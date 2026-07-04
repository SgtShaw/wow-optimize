// ============================================================================
// Module: async_culling.cpp
// Description: Speculative async frustum culling cache offloaded to worker threads.
// Safety & Threading: Lock-free caches, thread-safe snapshots, read-only fallbacks.
// ============================================================================

#include "async_culling.h"
#include "MinHook.h"
#include "version.h"
#include <atomic>
#include <vector>

extern "C" void Log(const char* fmt, ...);

namespace AsyncCulling {

// Addresses of frustum culling APIs in WoW 3.3.5a
constexpr uintptr_t ADDR_IS_SPHERE_VISIBLE_1 = 0x009839E0; // CFrustum::IsSphereVisible Type 1
constexpr uintptr_t ADDR_IS_SPHERE_VISIBLE_2 = 0x00983A60; // CFrustum::IsSphereVisible Type 2
constexpr uintptr_t ADDR_IS_POINT_VISIBLE    = 0x00983D70; // CFrustum::IsPointVisible

// Original function pointers
typedef int (__thiscall *IsSphereVisible1_fn)(void* This, void* sphere);
static IsSphereVisible1_fn orig_IsSphereVisible1 = nullptr;

typedef int (__thiscall *IsSphereVisible2_fn)(void* This, void* sphere);
static IsSphereVisible2_fn orig_IsSphereVisible2 = nullptr;

typedef void (__thiscall *IsPointVisible_fn)(void* This, const float* point, uint8_t* outMask);
static IsPointVisible_fn orig_IsPointVisible = nullptr;

// Cache parameters
static constexpr size_t CACHE_SIZE = 16384;
static constexpr size_t CACHE_MASK = CACHE_SIZE - 1;

struct CacheEntry {
    std::atomic<uint32_t> keyHash{0};
    float x, y, z, r;
    int result;
    std::atomic<uint32_t> frameGen{0};
};

static CacheEntry g_cullingCache[CACHE_SIZE];
static std::atomic<uint32_t> g_cullingFrameGen{1};

// Query history from previous frame
struct CullingQuery {
    uintptr_t funcAddr;
    float x, y, z, r;
};

static constexpr size_t HISTORY_MAX = 2048;
static CullingQuery g_queryHistory[HISTORY_MAX];
static std::atomic<size_t> g_queryCount{0};

// Frustum snapshot
static char g_frustumSnapshot[256];
static std::atomic<bool> g_frustumReady{false};

// Worker thread pool state
static HANDLE g_workerThreads[2] = {nullptr, nullptr};
static HANDLE g_taskEvent = nullptr;
static std::atomic<bool> g_shutdown{false};

// Task queue for worker threads
struct CullingTask {
    uintptr_t funcAddr;
    float x, y, z, r;
    uint32_t frameGen;
};

static constexpr size_t TASK_QUEUE_SIZE = 4096;
static constexpr size_t TASK_QUEUE_MASK = TASK_QUEUE_SIZE - 1;
static CullingTask g_taskQueue[TASK_QUEUE_SIZE];
static std::atomic<size_t> g_taskHead{0};
static std::atomic<size_t> g_taskTail{0};

// Simple Jenkins One-at-a-time hash for coordinates
static inline uint32_t HashCoords(uintptr_t func, float x, float y, float z, float r) {
    uint32_t hash = 0;
    const char* bytes = (const char*)&x;
    for (size_t i = 0; i < 16; i++) {
        hash += bytes[i % 4]; // simple mix
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (uint32_t)func;
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

// Record a query in history to pre-cull next frame
static void RecordQuery(uintptr_t func, float x, float y, float z, float r) {
    size_t idx = g_queryCount.fetch_add(1, std::memory_order_relaxed);
    if (idx < HISTORY_MAX) {
        g_queryHistory[idx] = {func, x, y, z, r};
    }
}

// Enqueue a task for background threads
static void EnqueueTask(uintptr_t func, float x, float y, float z, float r, uint32_t frameGen) {
    size_t tail = g_taskTail.load(std::memory_order_relaxed);
    size_t head = g_taskHead.load(std::memory_order_acquire);
    
    if (((tail + 1) & TASK_QUEUE_MASK) == (head & TASK_QUEUE_MASK)) {
        return; // Queue full
    }
    
    size_t slot = tail & TASK_QUEUE_MASK;
    g_taskQueue[slot] = {func, x, y, z, r, frameGen};
    
    g_taskTail.store(tail + 1, std::memory_order_release);
    SetEvent(g_taskEvent);
}

// Background worker thread logic
static DWORD WINAPI WorkerThreadProc(LPVOID) {
    while (!g_shutdown.load(std::memory_order_relaxed)) {
        WaitForSingleObject(g_taskEvent, INFINITE);
        if (g_shutdown.load(std::memory_order_relaxed)) break;
        
        size_t head = g_taskHead.load(std::memory_order_relaxed);
        size_t tail = g_taskTail.load(std::memory_order_acquire);
        
        while (head != tail) {
            size_t slot = head & TASK_QUEUE_MASK;
            CullingTask task = g_taskQueue[slot];
            
            // Advance head
            head++;
            g_taskHead.store(head, std::memory_order_release);
            
            // Process task if it belongs to the current frame generation
            uint32_t currentGen = g_cullingFrameGen.load(std::memory_order_acquire);
            if (task.frameGen == currentGen && g_frustumReady.load(std::memory_order_acquire)) {
                // Prepare arguments: we construct a local fake sphere/point block
                // since the original function reads from the pointer we pass.
                // WoW's sphere structure is: Vector3D center, float radius.
                float sphere[4] = {task.x, task.y, task.z, task.r};
                
                int res = 0;
                __try {
                    if (task.funcAddr == ADDR_IS_SPHERE_VISIBLE_1) {
                        res = orig_IsSphereVisible1(g_frustumSnapshot, sphere);
                    } else if (task.funcAddr == ADDR_IS_SPHERE_VISIBLE_2) {
                        res = orig_IsSphereVisible2(g_frustumSnapshot, sphere);
                    } else if (task.funcAddr == ADDR_IS_POINT_VISIBLE) {
                        uint8_t tempMask = 0;
                        orig_IsPointVisible(g_frustumSnapshot, sphere, &tempMask);
                        res = tempMask;
                    }
                    
                    // Write to cache
                    uint32_t hash = HashCoords(task.funcAddr, task.x, task.y, task.z, task.r);
                    size_t cacheSlot = hash & CACHE_MASK;
                    CacheEntry& entry = g_cullingCache[cacheSlot];
                    
                    entry.x = task.x;
                    entry.y = task.y;
                    entry.z = task.z;
                    entry.r = task.r;
                    entry.result = res;
                    entry.keyHash.store(hash, std::memory_order_relaxed);
                    entry.frameGen.store(currentGen, std::memory_order_release);
                }
                __except(EXCEPTION_EXECUTE_HANDLER) {
                    // Safe catch
                }
            }
        }
        
        ResetEvent(g_taskEvent);
    }
    return 0;
}

// Hooked IsSphereVisible Type 1
static int __fastcall Hooked_IsSphereVisible1(void* This, void* unused, void* sphere) {
    if (!This || !sphere) return 0;
    
    // Copy coordinates safely
    float* s = (float*)sphere;
    float x = s[0];
    float y = s[1];
    float z = s[2];
    float r = s[3];
    
    uint32_t currentGen = g_cullingFrameGen.load(std::memory_order_acquire);
    uint32_t hash = HashCoords(ADDR_IS_SPHERE_VISIBLE_1, x, y, z, r);
    size_t slot = hash & CACHE_MASK;
    CacheEntry& entry = g_cullingCache[slot];
    
    // Check cache
    if (entry.keyHash.load(std::memory_order_relaxed) == hash &&
        entry.x == x && entry.y == y && entry.z == z && entry.r == r &&
        entry.frameGen.load(std::memory_order_acquire) == currentGen) 
    {
        return entry.result;
    }
    
    // Snapshot frustum if not already done
    if (!g_frustumReady.load(std::memory_order_relaxed)) {
        memcpy(g_frustumSnapshot, This, 128);
        g_frustumReady.store(true, std::memory_order_release);
    }
    
    // Read-only fallback: compute synchronously on main thread
    int res = orig_IsSphereVisible1(This, sphere);
    
    // Save to cache
    entry.x = x;
    entry.y = y;
    entry.z = z;
    entry.r = r;
    entry.result = res;
    entry.keyHash.store(hash, std::memory_order_relaxed);
    entry.frameGen.store(currentGen, std::memory_order_release);
    
    // Record query for the next frame
    RecordQuery(ADDR_IS_SPHERE_VISIBLE_1, x, y, z, r);
    
    return res;
}

// Hooked IsSphereVisible Type 2
static int __fastcall Hooked_IsSphereVisible2(void* This, void* unused, void* sphere) {
    if (!This || !sphere) return 0;
    
    float* s = (float*)sphere;
    float x = s[0];
    float y = s[1];
    float z = s[2];
    float r = s[3];
    
    uint32_t currentGen = g_cullingFrameGen.load(std::memory_order_acquire);
    uint32_t hash = HashCoords(ADDR_IS_SPHERE_VISIBLE_2, x, y, z, r);
    size_t slot = hash & CACHE_MASK;
    CacheEntry& entry = g_cullingCache[slot];
    
    if (entry.keyHash.load(std::memory_order_relaxed) == hash &&
        entry.x == x && entry.y == y && entry.z == z && entry.r == r &&
        entry.frameGen.load(std::memory_order_acquire) == currentGen) 
    {
        return entry.result;
    }
    
    if (!g_frustumReady.load(std::memory_order_relaxed)) {
        memcpy(g_frustumSnapshot, This, 128);
        g_frustumReady.store(true, std::memory_order_release);
    }
    
    int res = orig_IsSphereVisible2(This, sphere);
    
    entry.x = x;
    entry.y = y;
    entry.z = z;
    entry.r = r;
    entry.result = res;
    entry.keyHash.store(hash, std::memory_order_relaxed);
    entry.frameGen.store(currentGen, std::memory_order_release);
    
    RecordQuery(ADDR_IS_SPHERE_VISIBLE_2, x, y, z, r);
    
    return res;
}

// Hooked IsPointVisible
static void __fastcall Hooked_IsPointVisible(void* This, void* unused, const float* point, uint8_t* outMask) {
    if (!This || !point) return;
    
    float x = point[0];
    float y = point[1];
    float z = point[2];
    float r = 0.0f; // Points have radius 0
    
    uint32_t currentGen = g_cullingFrameGen.load(std::memory_order_acquire);
    uint32_t hash = HashCoords(ADDR_IS_POINT_VISIBLE, x, y, z, r);
    size_t slot = hash & CACHE_MASK;
    CacheEntry& entry = g_cullingCache[slot];
    
    if (entry.keyHash.load(std::memory_order_relaxed) == hash &&
        entry.x == x && entry.y == y && entry.z == z && entry.r == r &&
        entry.frameGen.load(std::memory_order_acquire) == currentGen) 
    {
        if (outMask) {
            *outMask = (uint8_t)entry.result;
        }
        return;
    }
    
    if (!g_frustumReady.load(std::memory_order_relaxed)) {
        memcpy(g_frustumSnapshot, This, 128);
        g_frustumReady.store(true, std::memory_order_release);
    }
    
    orig_IsPointVisible(This, point, outMask);
    
    int res = outMask ? *outMask : 0;
    
    entry.x = x;
    entry.y = y;
    entry.z = z;
    entry.r = r;
    entry.result = res;
    entry.keyHash.store(hash, std::memory_order_relaxed);
    entry.frameGen.store(currentGen, std::memory_order_release);
    
    RecordQuery(ADDR_IS_POINT_VISIBLE, x, y, z, r);
}

bool Init() {
    g_shutdown.store(false);
    g_frustumReady.store(false);
    g_queryCount.store(0);
    g_taskHead.store(0);
    g_taskTail.store(0);
    
    memset(g_cullingCache, 0, sizeof(g_cullingCache));
    
    // Create task event and spawn culling threads
    g_taskEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_taskEvent) return false;
    
    for (int i = 0; i < 2; i++) {
        g_workerThreads[i] = CreateThread(NULL, 0, WorkerThreadProc, NULL, 0, NULL);
        if (!g_workerThreads[i]) {
            Shutdown();
            return false;
        }
    }
    
    // Hook the culling functions
    if (MH_CreateHook((void*)ADDR_IS_SPHERE_VISIBLE_1, (void*)Hooked_IsSphereVisible1, (void**)&orig_IsSphereVisible1) != MH_OK ||
        MH_CreateHook((void*)ADDR_IS_SPHERE_VISIBLE_2, (void*)Hooked_IsSphereVisible2, (void**)&orig_IsSphereVisible2) != MH_OK ||
        MH_CreateHook((void*)ADDR_IS_POINT_VISIBLE, (void*)Hooked_IsPointVisible, (void**)&orig_IsPointVisible) != MH_OK) 
    {
        Shutdown();
        return false;
    }
    
    MH_EnableHook((void*)ADDR_IS_SPHERE_VISIBLE_1);
    MH_EnableHook((void*)ADDR_IS_SPHERE_VISIBLE_2);
    MH_EnableHook((void*)ADDR_IS_POINT_VISIBLE);
    
    Log("[AsyncCulling] Hooks successfully installed and worker threads active");
    return true;
}

void Shutdown() {
    g_shutdown.store(true);
    if (g_taskEvent) SetEvent(g_taskEvent);
    
    for (int i = 0; i < 2; i++) {
        if (g_workerThreads[i]) {
            WaitForSingleObject(g_workerThreads[i], 2000);
            CloseHandle(g_workerThreads[i]);
            g_workerThreads[i] = nullptr;
        }
    }
    
    if (g_taskEvent) {
        CloseHandle(g_taskEvent);
        g_taskEvent = nullptr;
    }
    
    MH_DisableHook((void*)ADDR_IS_SPHERE_VISIBLE_1);
    MH_DisableHook((void*)ADDR_IS_SPHERE_VISIBLE_2);
    MH_DisableHook((void*)ADDR_IS_POINT_VISIBLE);
}

void OnFrameStart() {
    // Increment generation
    uint32_t currentGen = g_cullingFrameGen.fetch_add(1, std::memory_order_relaxed) + 1;
    
    // Reset frustum snapshot flag
    g_frustumReady.store(false, std::memory_order_release);
    
    // Grab query history and enqueue tasks to pre-cull in parallel
    size_t count = g_queryCount.exchange(0, std::memory_order_relaxed);
    if (count > HISTORY_MAX) count = HISTORY_MAX;
    
    // Clear background queue
    g_taskHead.store(0, std::memory_order_relaxed);
    g_taskTail.store(0, std::memory_order_relaxed);
    
    for (size_t i = 0; i < count; i++) {
        CullingQuery q = g_queryHistory[i];
        EnqueueTask(q.funcAddr, q.x, q.y, q.z, q.r, currentGen);
    }
}

} // namespace AsyncCulling
