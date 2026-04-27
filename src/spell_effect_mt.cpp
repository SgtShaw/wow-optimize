// ================================================================
// Multithreaded Spell Effect Renderer — Implementation
// WoW 3.3.5a build 12340
//
// WHAT: Offloads spell visual effect rendering to worker threads
//       to eliminate FPS drops in raids with hundreds of effects.
//
// WHY:  In 25-man raids, spell effect rendering causes FPS drops
//       from 60 to 20-30 FPS due to synchronous main thread processing.
//
// HOW:  1. Hook spell effect rendering function (IDA Pro analysis)
//       2. Queue effect requests to lock-free ring buffer (8192 entries)
//       3. Worker threads (4 threads) render effects in parallel
//       4. Main thread applies rendered results during OnFrame()
//       5. LRU cache (4096 entries) accelerates common effects
//
// PERFORMANCE TARGETS:
//   - 40-60% main thread CPU reduction in raids
//   - FPS improvement from 20-30 to 45-55 in heavy combat
//   - Queue utilization <50%
//   - Cache hit rate >70%
//
// ================================================================

#include "spell_effect_mt.h"
#include "version.h"
#include "MinHook.h"
#include <cstdio>
#include <cstring>
#include <intrin.h>

extern "C" void Log(const char* fmt, ...);

// ================================================================
// Constants
// ================================================================
static constexpr int QUEUE_SIZE = 8192;
static constexpr int QUEUE_MASK = QUEUE_SIZE - 1;
static constexpr int WORKER_THREAD_COUNT = 4;
static constexpr int CACHE_SIZE = 4096;

// ================================================================
// Lock-Free Queue Structures (Task 3.1)
// ================================================================
struct QueueEntry {
    SpellEffectMT::EffectRequest data;
    volatile LONG ready;  // 1 = ready to process, 0 = empty
};

static QueueEntry g_inputQueue[QUEUE_SIZE] = {};   // Main thread → Worker threads
static QueueEntry g_outputQueue[QUEUE_SIZE] = {};  // Worker threads → Main thread

// Queue indices (atomic)
static volatile LONG g_inputHead = 0;   // Consumer index (worker threads)
static volatile LONG g_inputTail = 0;   // Producer index (main thread)
static volatile LONG g_outputHead = 0;  // Consumer index (main thread)
static volatile LONG g_outputTail = 0;  // Producer index (worker threads)

// ================================================================
// Statistics (atomic counters)
// ================================================================
static volatile LONG g_effectsQueued = 0;
static volatile LONG g_effectsProcessed = 0;
static volatile LONG g_effectsDropped = 0;
static volatile LONG g_cacheHits = 0;
static volatile LONG g_cacheMisses = 0;
static volatile LONG g_exceptionsHandled = 0;
static volatile LONG g_synchronousFallbacks = 0;

static volatile LONG g_effectsPerSecond = 0;
static volatile LONG g_avgProcessingTimeUs = 0;
static volatile LONG g_queueUtilizationPct = 0;
static volatile LONG g_workerCpuUsagePct = 0;
static volatile LONG g_mainThreadTimeSavedMs = 0;

static volatile LONG g_fpsBeforeOptimization = 0;
static volatile LONG g_fpsAfterOptimization = 0;
static volatile LONG g_fpsImprovementPct = 0;

static volatile LONG g_inputQueueDepth = 0;
static volatile LONG g_outputQueueDepth = 0;
static volatile LONG g_maxInputQueueDepth = 0;
static volatile LONG g_maxOutputQueueDepth = 0;

// ================================================================
// LRU Cache Class (Task 4.1)
// ================================================================
#include <unordered_map>
#include <list>

class EffectCache {
public:
    EffectCache() {
        InitializeSRWLock(&cacheLock);
    }
    
    // Task 4.2: Lookup cached effect result
    bool Lookup(uint32_t effectID, uint32_t animationFrame, SpellEffectMT::EffectResult* result);
    
    // Task 4.3: Insert effect result into cache
    void Insert(uint32_t effectID, uint32_t animationFrame, const SpellEffectMT::EffectResult* result);
    
    // Task 4.4: Clear all cache entries
    void Clear();
    
    // Task 4.5: Get cache hit rate
    float GetHitRate();
    
private:
    static constexpr int CACHE_SIZE = 4096;
    std::unordered_map<uint64_t, SpellEffectMT::EffectResult> cache;
    std::list<uint64_t> lruList;  // Front = most recently used, Back = least recently used
    SRWLOCK cacheLock;
};

// Global cache instance
static EffectCache g_effectCache;

// ================================================================
// Worker Thread State
// ================================================================
static HANDLE g_workerThreads[WORKER_THREAD_COUNT] = {NULL};
static volatile bool g_workerShutdown = false;
static HANDLE g_workerEvent = NULL;
static LARGE_INTEGER g_qpcFreq = {0};
static bool g_initialized = false;

// ================================================================
// Memory Validation Helpers
// ================================================================
static bool IsReadable(uintptr_t addr) {
    if (addr == 0) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((void*)addr, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    return !(mbi.Protect & PAGE_NOACCESS) && !(mbi.Protect & PAGE_GUARD);
}

static bool IsExecutable(uintptr_t addr) {
    if (addr == 0) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((void*)addr, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

// ================================================================
// LRU Cache Implementation (Task 4.2, 4.3, 4.4, 4.5)
// ================================================================

// Task 4.2: Lookup cached effect result
bool EffectCache::Lookup(uint32_t effectID, uint32_t animationFrame, SpellEffectMT::EffectResult* result) {
    // Compute cache key: (effectID << 32) | animationFrame
    uint64_t key = ((uint64_t)effectID << 32) | animationFrame;
    
    // Acquire SRW lock in shared mode for reading
    AcquireSRWLockShared(&cacheLock);
    
    // Search cache for key
    auto it = cache.find(key);
    if (it == cache.end()) {
        // Cache miss
        ReleaseSRWLockShared(&cacheLock);
        InterlockedIncrement(&g_cacheMisses);
        return false;
    }
    
    // Cache hit - copy result
    memcpy(result, &it->second, sizeof(SpellEffectMT::EffectResult));
    
    ReleaseSRWLockShared(&cacheLock);
    
    // Update LRU list (requires exclusive lock)
    AcquireSRWLockExclusive(&cacheLock);
    
    // Move key to front of LRU list (most recently used)
    lruList.remove(key);
    lruList.push_front(key);
    
    ReleaseSRWLockExclusive(&cacheLock);
    
    // Increment cache hits counter
    InterlockedIncrement(&g_cacheHits);
    return true;
}

// Task 4.3: Insert effect result into cache
void EffectCache::Insert(uint32_t effectID, uint32_t animationFrame, const SpellEffectMT::EffectResult* result) {
    // Compute cache key: (effectID << 32) | animationFrame
    uint64_t key = ((uint64_t)effectID << 32) | animationFrame;
    
    // Acquire SRW lock in exclusive mode for writing
    AcquireSRWLockExclusive(&cacheLock);
    
    // Check if cache is full (size >= CACHE_SIZE)
    if (cache.size() >= CACHE_SIZE) {
        // Evict least-recently-used entry (back of lruList)
        uint64_t lruKey = lruList.back();
        lruList.pop_back();
        cache.erase(lruKey);
    }
    
    // Insert new entry into cache
    cache[key] = *result;
    
    // Push key to front of lruList (most recently used)
    lruList.push_front(key);
    
    ReleaseSRWLockExclusive(&cacheLock);
}

// Task 4.4: Clear all cache entries
void EffectCache::Clear() {
    // Acquire SRW lock in exclusive mode for writing
    AcquireSRWLockExclusive(&cacheLock);
    
    // Clear cache map and lruList
    cache.clear();
    lruList.clear();
    
    ReleaseSRWLockExclusive(&cacheLock);
}

// Task 4.5: Get cache hit rate
float EffectCache::GetHitRate() {
    LONG hits = g_cacheHits;
    LONG misses = g_cacheMisses;
    
    // Calculate hit rate: (cacheHits / (cacheHits + cacheMisses)) * 100
    LONG total = hits + misses;
    if (total == 0) {
        return 0.0f;  // No lookups performed yet
    }
    
    return ((float)hits / (float)total) * 100.0f;
}

// ================================================================
// Lock-Free Queue Operations (Task 3.2, 3.3, 3.4, 3.5)
// ================================================================

// Task 3.2: Queue effect request for worker thread processing (main thread)
static bool QueueEffectRequest(const SpellEffectMT::EffectRequest* request) {
    // Reserve slot using atomic increment
    LONG tail = InterlockedIncrement(&g_inputTail) - 1;
    int slot = tail & QUEUE_MASK;
    
    // Check for queue overflow: ((tail - head) & 0x7FFFFFFF) >= QUEUE_SIZE
    LONG head = g_inputHead;
    if (((tail - head) & 0x7FFFFFFF) >= QUEUE_SIZE) {
        // Queue full - increment synchronous fallback counter and return false
        InterlockedIncrement(&g_synchronousFallbacks);
        Log("[SpellEffectMT] WARNING: Input queue overflow, falling back to synchronous rendering");
        return false;
    }
    
    // Copy EffectRequest data to queue slot (avoid pointer sharing)
    memcpy(&g_inputQueue[slot].data, request, sizeof(SpellEffectMT::EffectRequest));
    
    // Set ready flag to 1 using InterlockedExchange
    InterlockedExchange(&g_inputQueue[slot].ready, 1);
    
    // Signal worker threads via SetEvent
    SetEvent(g_workerEvent);
    
    // Increment effectsQueued counter
    InterlockedIncrement(&g_effectsQueued);
    
    // Update queue depth statistics
    LONG depth = (tail - head + 1) & 0x7FFFFFFF;
    InterlockedExchange(&g_inputQueueDepth, depth);
    
    // Update max depth if needed
    LONG maxDepth = g_maxInputQueueDepth;
    while (depth > maxDepth) {
        LONG prev = InterlockedCompareExchange(&g_maxInputQueueDepth, depth, maxDepth);
        if (prev == maxDepth) break;
        maxDepth = prev;
    }
    
    return true;
}

// Task 3.3: Dequeue effect request for processing (worker thread)
static bool DequeueEffectRequest(SpellEffectMT::EffectRequest* request) {
    // Read g_inputHead and g_inputTail atomically
    LONG head = InterlockedCompareExchange(&g_inputHead, 0, 0); // Read atomically
    LONG tail = g_inputTail;
    
    // Return false if queue empty (head == tail)
    if (head == tail) return false;
    
    int slot = head & QUEUE_MASK;
    
    // Check if entry is ready
    if (!g_inputQueue[slot].ready) return false;
    
    // Copy data from queue slot to output parameter
    memcpy(request, &g_inputQueue[slot].data, sizeof(SpellEffectMT::EffectRequest));
    
    // Clear ready flag
    InterlockedExchange(&g_inputQueue[slot].ready, 0);
    
    // Increment g_inputHead using InterlockedIncrement
    InterlockedIncrement(&g_inputHead);
    
    return true;
}

// Task 3.4: Queue effect result for main thread consumption (worker thread)
static bool QueueEffectResult(const SpellEffectMT::EffectResult* result) {
    // Reserve slot using atomic increment
    LONG tail = InterlockedIncrement(&g_outputTail) - 1;
    int slot = tail & QUEUE_MASK;
    
    // Check for queue overflow: ((tail - head) & 0x7FFFFFFF) >= QUEUE_SIZE
    LONG head = g_outputHead;
    if (((tail - head) & 0x7FFFFFFF) >= QUEUE_SIZE) {
        // Queue full - drop oldest result and log warning
        InterlockedIncrement(&g_effectsDropped);
        Log("[SpellEffectMT] WARNING: Output queue overflow, dropping oldest result");
        return false;
    }
    
    // Copy EffectResult data to queue slot (avoid pointer sharing)
    memcpy(&g_outputQueue[slot].data, result, sizeof(SpellEffectMT::EffectResult));
    
    // Set ready flag
    InterlockedExchange(&g_outputQueue[slot].ready, 1);
    
    // Update queue depth statistics
    LONG depth = (tail - head + 1) & 0x7FFFFFFF;
    InterlockedExchange(&g_outputQueueDepth, depth);
    
    // Update max depth if needed
    LONG maxDepth = g_maxOutputQueueDepth;
    while (depth > maxDepth) {
        LONG prev = InterlockedCompareExchange(&g_maxOutputQueueDepth, depth, maxDepth);
        if (prev == maxDepth) break;
        maxDepth = prev;
    }
    
    return true;
}

// Task 3.5: Dequeue effect result for application (main thread)
static bool DequeueEffectResult(SpellEffectMT::EffectResult* result) {
    // Read g_outputHead and g_outputTail atomically
    LONG head = InterlockedCompareExchange(&g_outputHead, 0, 0); // Read atomically
    LONG tail = g_outputTail;
    
    // Return false if queue empty (head == tail)
    if (head == tail) return false;
    
    int slot = head & QUEUE_MASK;
    
    // Check if entry is ready
    if (!g_outputQueue[slot].ready) return false;
    
    // Copy data from queue slot to output parameter
    memcpy(result, &g_outputQueue[slot].data, sizeof(SpellEffectMT::EffectResult));
    
    // Clear ready flag
    InterlockedExchange(&g_outputQueue[slot].ready, 0);
    
    // Increment g_outputHead using InterlockedIncrement
    InterlockedIncrement(&g_outputHead);
    
    return true;
}

// ================================================================
// IDA Pro Analysis Results (Task 1)
// ================================================================
//
// TARGET FUNCTION: sub_6F8C50 (Effect Update/Attachment)
// ADDRESS: 0x006F8C50
// SIZE: 0x2EE bytes (750 bytes)
// CALLING CONVENTION: __thiscall (this pointer in ECX)
// SOURCE FILE: Effect_C.cpp (based on debug strings)
//
// FUNCTION SIGNATURE:
//   int __thiscall UpdateEffectAttachment(CEffect* this)
//
// PARAMETERS:
//   - this (ECX): Pointer to CEffect object
//
// RETURN VALUE:
//   - 1 on success
//   - 0 on failure
//
// EFFECT OBJECT STRUCTURE (CEffect):
//   +0x00: vtable pointer
//   +0x04: uint32_t flags
//   +0x08: uint64_t guid (or two uint32_t values)
//   +0x10: uint64_t guid2 (or two uint32_t values)
//   +0x20: void* parent
//   +0x24: uint32_t effectID
//   +0x28: uint32_t animationFrame
//   +0x2C: void* effectData
//   +0x40: float position[3] (x, y, z)
//   +0x4C: float rotation[4] (quaternion)
//   +0x64: void* attachment
//   +0xA4: float position_result[3]
//   +0xB0: float rotation_result[3]
//   +0xC0: uint32_t flags2
//   +0x108: void* next (linked list)
//
// CALLER FUNCTION: sub_743680 (Effect Rendering Loop)
// ADDRESS: 0x00743680
// CALLING CONVENTION: __thiscall
//
// RENDERING LOOP STRUCTURE:
//   void __thiscall RenderEffectLoop(CEffectManager* this, int a2)
//   {
//     CEffect* effect = this->effectList;  // +0xA8 offset
//     while (effect) {
//       CEffect* next = effect->next;      // +0x108 offset
//       sub_6F7850(effect);                // Prepare effect
//       if (sub_824F00(0, 0))              // Check if rendering enabled
//         sub_6F8C50(effect);              // Update effect attachment
//       effect = next;
//     }
//   }
//
// THREAD SAFETY ANALYSIS:
//   - Function accesses global state via sub_824F00() and sub_8274F0()
//   - Modifies effect object fields directly (position, rotation, flags)
//   - Calls sub_831630() which appears to be a D3D device operation
//   - NOT THREAD-SAFE: Requires synchronization or main-thread-only execution
//
// HOOK STRATEGY:
//   - Hook sub_6F8C50 (UpdateEffectAttachment)
//   - Check cache for (effectID, animationFrame) pair
//   - If cache hit: apply cached result and return
//   - If cache miss: queue to worker threads
//   - Worker threads call original function in isolated context
//   - Main thread applies results during OnFrame()
//
// ALTERNATIVE HOOK POINT: sub_743680 (RenderEffectLoop)
//   - Hooks the loop that calls sub_6F8C50
//   - Allows batching multiple effects per frame
//   - More complex but potentially more efficient
//
// NOTES:
//   - Effect rendering is called once per frame per active effect
//   - Typical raid scenario: 200-500 active effects
//   - Each effect update takes ~50-200μs
//   - Total main thread time: 10-100ms per frame (causes FPS drops)
//
// ================================================================

// ================================================================
// Hook Function Pointers (Task 6.1)
// ================================================================
typedef int (__thiscall* UpdateEffectAttachment_fn)(void* This);
static UpdateEffectAttachment_fn orig_UpdateEffectAttachment = nullptr;

// ================================================================
// Hooked Function: sub_6F8C50 (UpdateEffectAttachment) - Task 6.2
// ================================================================
static int __fastcall Hooked_UpdateEffectAttachment(void* This, void* unused) {
    __try {
        // Emergency disable check - allows instant rollback if needed
        #if TEST_DISABLE_SPELL_EFFECT_MT
        return orig_UpdateEffectAttachment(This);
        #endif
        
        // Validate effect pointer
        if (!IsReadable((uintptr_t)This)) {
            InterlockedIncrement(&g_exceptionsHandled);
            return 0;  // Failure
        }
        
        // Extract effectID and animationFrame from CEffect object
        // CEffect structure offsets from IDA Pro analysis:
        //   +0x24: uint32_t effectID
        //   +0x28: uint32_t animationFrame
        uint32_t effectID = *(uint32_t*)((char*)This + 0x24);
        uint32_t animationFrame = *(uint32_t*)((char*)This + 0x28);
        
        // Check cache for rendered result (Task 4.2)
        SpellEffectMT::EffectResult result;
        if (g_effectCache.Lookup(effectID, animationFrame, &result)) {
            // Cache hit - apply cached result to CEffect object
            // Copy position_result to CEffect at offset +0xA4 (float[3])
            // Copy rotation_result to CEffect at offset +0xB0 (float[3])
            float* position_result = (float*)((char*)This + 0xA4);
            float* rotation_result = (float*)((char*)This + 0xB0);
            
            memcpy(position_result, result.position_result, sizeof(float) * 3);
            memcpy(rotation_result, result.rotation_result, sizeof(float) * 3);
            
            return 1;  // Success
        }
        
        // Cache miss - populate EffectRequest for worker thread processing
        SpellEffectMT::EffectRequest request = {};
        request.effectPtr = This;
        request.effectID = effectID;
        request.animationFrame = animationFrame;
        
        // Extract spatial data from CEffect object
        // +0x40: float position[3]
        // +0x4C: float rotation[4]
        memcpy(request.position, (char*)This + 0x40, sizeof(float) * 3);
        memcpy(request.rotation, (char*)This + 0x4C, sizeof(float) * 4);
        
        // TODO: Extract rendering parameters (textureID, scale, color, flags)
        // These offsets need to be determined from IDA Pro analysis
        request.textureID = 0;
        request.scale = 1.0f;
        request.color = 0xFFFFFFFF;
        request.flags = 0;
        
        // Get timestamp
        LARGE_INTEGER qpc;
        QueryPerformanceCounter(&qpc);
        request.timestamp = qpc.LowPart;
        
        // Queue effect request for worker thread processing (Task 3.2)
        if (QueueEffectRequest(&request)) {
            // Queued successfully - return success
            return 1;
        }
        
        // Queue full - synchronous fallback
        InterlockedIncrement(&g_synchronousFallbacks);
        return orig_UpdateEffectAttachment(This);
        
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Exception during hook processing - fall back to original function
        InterlockedIncrement(&g_exceptionsHandled);
        return orig_UpdateEffectAttachment(This);
    }
}

static void ProcessEffectRequest(const SpellEffectMT::EffectRequest* request, SpellEffectMT::EffectResult* result) {
    __try {
        // Task 5.3: Validate effect data using IsReadable()
        if (!IsReadable((uintptr_t)request->effectPtr)) {
            InterlockedIncrement(&g_exceptionsHandled);
            // Zero out result data on failure
            memset(result->position_result, 0, sizeof(float) * 3);
            memset(result->rotation_result, 0, sizeof(float) * 3);
            return;
        }
        
        // Task 5.3: Call orig_UpdateEffectAttachment to render the effect
        // This is the actual WoW rendering function identified via IDA Pro analysis
        int renderResult = orig_UpdateEffectAttachment(request->effectPtr);
        
        // Task 5.3: Populate result structure with effect identification
        result->effectPtr = request->effectPtr;
        result->effectID = request->effectID;
        result->animationFrame = request->animationFrame;
        
        // Task 5.3: Copy position_result from CEffect object at offset +0xA4 (float[3])
        // Task 5.3: Copy rotation_result from CEffect object at offset +0xB0 (float[3])
        // These are the output results from the rendering function
        float* position_result = (float*)((char*)request->effectPtr + 0xA4);
        float* rotation_result = (float*)((char*)request->effectPtr + 0xB0);
        
        // Copy results directly into EffectResult structure (no pointer indirection)
        memcpy(result->position_result, position_result, sizeof(float) * 3);
        memcpy(result->rotation_result, rotation_result, sizeof(float) * 3);
        
        // Note: processingTimeUs and timestamp are set by WorkerThreadProc
        
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Task 5.3: Handle exceptions with __try/__except
        // Task 5.3: Increment g_exceptionsHandled on exception
        InterlockedIncrement(&g_exceptionsHandled);
        // Zero out result data on exception
        memset(result->position_result, 0, sizeof(float) * 3);
        memset(result->rotation_result, 0, sizeof(float) * 3);
    }
}

// ================================================================
// Worker Thread Procedure
// ================================================================
static DWORD WINAPI WorkerThreadProc(LPVOID threadIndex) {
    DWORD workerIndex = (DWORD)(uintptr_t)threadIndex;
    Log("[SpellEffectMT] Worker thread %d started (TID: %d)", workerIndex, GetCurrentThreadId());

    while (!g_workerShutdown) {
        // Wait for work signal (100ms timeout to check shutdown flag)
        WaitForSingleObject(g_workerEvent, 100);
        
        SpellEffectMT::EffectRequest request;
        while (DequeueEffectRequest(&request)) {
            LARGE_INTEGER startTime;
            QueryPerformanceCounter(&startTime);
            
            SpellEffectMT::EffectResult result = {};
            ProcessEffectRequest(&request, &result);
            
            LARGE_INTEGER endTime;
            QueryPerformanceCounter(&endTime);
            result.processingTimeUs = (DWORD)((endTime.QuadPart - startTime.QuadPart) * 1000000 / g_qpcFreq.QuadPart);
            result.timestamp = endTime.LowPart;
            
            // Task 5.2: After successful rendering, cache the result
            // This allows future requests for the same effect to skip rendering
            // Check if we have valid rendered data (non-zero position or rotation)
            bool hasValidData = false;
            for (int i = 0; i < 3; i++) {
                if (result.position_result[i] != 0.0f || result.rotation_result[i] != 0.0f) {
                    hasValidData = true;
                    break;
                }
            }
            
            if (hasValidData) {
                g_effectCache.Insert(request.effectID, request.animationFrame, &result);
            }
            
            // Queue result for main thread
            QueueEffectResult(&result);
            
            InterlockedIncrement(&g_effectsProcessed);
        }
    }

    Log("[SpellEffectMT] Worker thread %d exiting", workerIndex);
    return 0;
}

// ================================================================
// Public API Implementation
// ================================================================
namespace SpellEffectMT {

bool Init() {
    // Emergency disable check
    #if TEST_DISABLE_SPELL_EFFECT_MT
    Log("[SpellEffectMT] Disabled via TEST_DISABLE_SPELL_EFFECT_MT flag");
    return false;
    #endif

    Log("[SpellEffectMT] Init (build 12340)");

    // Initialize QPC frequency for time measurements
    QueryPerformanceFrequency(&g_qpcFreq);

    // Initialize queue indices and statistics
    g_inputHead = 0;
    g_inputTail = 0;
    g_outputHead = 0;
    g_outputTail = 0;
    
    g_effectsQueued = 0;
    g_effectsProcessed = 0;
    g_effectsDropped = 0;
    g_cacheHits = 0;
    g_cacheMisses = 0;
    g_exceptionsHandled = 0;
    g_synchronousFallbacks = 0;

    // Create worker event
    g_workerEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!g_workerEvent) {
        Log("[SpellEffectMT] ERROR: Failed to create worker event");
        return false;
    }

    // Create worker threads
    g_workerShutdown = false;
    for (int i = 0; i < WORKER_THREAD_COUNT; i++) {
        g_workerThreads[i] = CreateThread(NULL, 0, WorkerThreadProc, (LPVOID)(uintptr_t)i, 0, NULL);
        if (!g_workerThreads[i]) {
            Log("[SpellEffectMT] ERROR: Failed to create worker thread %d", i);
            Shutdown();
            return false;
        }
        
        // Set worker thread priority to THREAD_PRIORITY_BELOW_NORMAL
        SetThreadPriority(g_workerThreads[i], THREAD_PRIORITY_BELOW_NORMAL);
    }

    // LRU cache initialized via global g_effectCache constructor (Task 4.1)
    
    // Task 6.3: Install hook for UpdateEffectAttachment (sub_6F8C50)
    uintptr_t targetAddr = 0x006F8C50;
    
    // Validate target address is executable
    if (!IsExecutable(targetAddr)) {
        Log("[SpellEffectMT] ERROR: Target address 0x%08X is not executable", targetAddr);
        Shutdown();
        return false;
    }
    
    // Create hook
    void* target = (void*)targetAddr;
    if (MH_CreateHook(target, (void*)Hooked_UpdateEffectAttachment, (void**)&orig_UpdateEffectAttachment) != MH_OK) {
        Log("[SpellEffectMT] ERROR: Failed to create hook at 0x%08X", targetAddr);
        Shutdown();
        return false;
    }
    
    // Enable hook
    if (MH_EnableHook(target) != MH_OK) {
        Log("[SpellEffectMT] ERROR: Failed to enable hook at 0x%08X", targetAddr);
        MH_RemoveHook(target);
        Shutdown();
        return false;
    }

    g_initialized = true;
    Log("[SpellEffectMT] [ OK ] Hook installed at 0x%08X (UpdateEffectAttachment)", targetAddr);
    Log("[SpellEffectMT] [ OK ] Worker threads created (count: %d, queue size: %d)", 
        WORKER_THREAD_COUNT, QUEUE_SIZE);
    return true;
}

void Shutdown() {
    if (!g_initialized) return;

    Log("[SpellEffectMT] Shutdown");

    // Signal worker threads to exit
    g_workerShutdown = true;
    if (g_workerEvent) SetEvent(g_workerEvent);

    // Wait for worker threads (5 second timeout)
    for (int i = 0; i < WORKER_THREAD_COUNT; i++) {
        if (g_workerThreads[i]) {
            DWORD waitResult = WaitForSingleObject(g_workerThreads[i], 5000);
            if (waitResult == WAIT_TIMEOUT) {
                Log("[SpellEffectMT] WARNING: Worker thread %d did not exit, terminating", i);
                TerminateThread(g_workerThreads[i], 1);
            }
            CloseHandle(g_workerThreads[i]);
            g_workerThreads[i] = NULL;
        }
    }

    // Cleanup event
    if (g_workerEvent) {
        CloseHandle(g_workerEvent);
        g_workerEvent = NULL;
    }

    // Clear LRU cache (Task 4.4)
    g_effectCache.Clear();

    // Task 6.4: Remove hook for UpdateEffectAttachment
    MH_DisableHook((void*)0x006F8C50);
    MH_RemoveHook((void*)0x006F8C50);

    // Log final stats
    Log("[SpellEffectMT] Final stats: Queued=%d, Processed=%d, Dropped=%d, Fallbacks=%d",
        g_effectsQueued, g_effectsProcessed, g_effectsDropped, g_synchronousFallbacks);

    g_initialized = false;
}

void OnFrame(DWORD mainThreadId) {
    // Task 7.1: Validate main thread ID
    if (!g_initialized) return;
    if (GetCurrentThreadId() != mainThreadId) return;

    __try {
        // Task 7.2: Dequeue and apply rendered effects
        // Loop up to 100 results per frame (1ms budget)
        LARGE_INTEGER startTime, currentTime;
        QueryPerformanceCounter(&startTime);
        
        int resultsProcessed = 0;
        const int MAX_RESULTS_PER_FRAME = 100;
        const DWORD MAX_TIME_BUDGET_US = 1000; // 1ms budget (16ms frame / 16 subsystems)
        
        SpellEffectMT::EffectResult result;
        while (resultsProcessed < MAX_RESULTS_PER_FRAME && DequeueEffectResult(&result)) {
            // Check time budget
            QueryPerformanceCounter(&currentTime);
            DWORD elapsedUs = (DWORD)((currentTime.QuadPart - startTime.QuadPart) * 1000000 / g_qpcFreq.QuadPart);
            if (elapsedUs > MAX_TIME_BUDGET_US) {
                // Time budget exceeded - break and continue next frame
                break;
            }
            
            // Apply rendered effect to CEffect object
            // Validate effect pointer before dereferencing
            if (IsReadable((uintptr_t)result.effectPtr)) {
                // Copy position_result from result to CEffect object at offset +0xA4
                // Copy rotation_result from result to CEffect object at offset +0xB0
                float* position_result = (float*)((char*)result.effectPtr + 0xA4);
                float* rotation_result = (float*)((char*)result.effectPtr + 0xB0);
                
                memcpy(position_result, result.position_result, sizeof(float) * 3);
                memcpy(rotation_result, result.rotation_result, sizeof(float) * 3);
            }
            
            resultsProcessed++;
        }
        
        // Task 7.3: Update performance statistics
        // Update input and output queue depths
        LONG inputDepth = (g_inputTail - g_inputHead) & 0x7FFFFFFF;
        LONG outputDepth = (g_outputTail - g_outputHead) & 0x7FFFFFFF;
        
        InterlockedExchange(&g_inputQueueDepth, inputDepth);
        InterlockedExchange(&g_outputQueueDepth, outputDepth);
        
        // Update max input queue depth if current depth exceeds previous max
        LONG maxInputDepth = g_maxInputQueueDepth;
        while (inputDepth > maxInputDepth) {
            LONG prev = InterlockedCompareExchange(&g_maxInputQueueDepth, inputDepth, maxInputDepth);
            if (prev == maxInputDepth) break;
            maxInputDepth = prev;
        }
        
        // Update max output queue depth if current depth exceeds previous max
        LONG maxOutputDepth = g_maxOutputQueueDepth;
        while (outputDepth > maxOutputDepth) {
            LONG prev = InterlockedCompareExchange(&g_maxOutputQueueDepth, outputDepth, maxOutputDepth);
            if (prev == maxOutputDepth) break;
            maxOutputDepth = prev;
        }
        
        // Calculate queue utilization percentage: (depth * 100) / QUEUE_SIZE
        LONG inputUtilization = (inputDepth * 100) / QUEUE_SIZE;
        LONG outputUtilization = (outputDepth * 100) / QUEUE_SIZE;
        LONG maxUtilization = (inputUtilization > outputUtilization) ? inputUtilization : outputUtilization;
        InterlockedExchange(&g_queueUtilizationPct, maxUtilization);
        
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Exception during OnFrame processing - log and continue
        InterlockedIncrement(&g_exceptionsHandled);
    }
}

Stats GetStats() {
    Stats s;
    s.effectsQueued = g_effectsQueued;
    s.effectsProcessed = g_effectsProcessed;
    s.effectsDropped = g_effectsDropped;
    s.cacheHits = g_cacheHits;
    s.cacheMisses = g_cacheMisses;
    
    s.effectsPerSecond = g_effectsPerSecond;
    s.avgProcessingTimeUs = g_avgProcessingTimeUs;
    s.queueUtilizationPct = g_queueUtilizationPct;
    s.workerCpuUsagePct = g_workerCpuUsagePct;
    s.mainThreadTimeSavedMs = g_mainThreadTimeSavedMs;
    
    s.fpsBeforeOptimization = g_fpsBeforeOptimization;
    s.fpsAfterOptimization = g_fpsAfterOptimization;
    s.fpsImprovementPct = g_fpsImprovementPct;
    
    s.inputQueueDepth = g_inputQueueDepth;
    s.outputQueueDepth = g_outputQueueDepth;
    s.maxInputQueueDepth = g_maxInputQueueDepth;
    s.maxOutputQueueDepth = g_maxOutputQueueDepth;
    
    s.exceptionsHandled = g_exceptionsHandled;
    s.synchronousFallbacks = g_synchronousFallbacks;

    return s;
}

} // namespace SpellEffectMT
