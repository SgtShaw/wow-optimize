// ================================================================
// Async Texture/Model Loader — Implementation
// WoW 3.3.5a build 12340
// ================================================================

#include "texture_async.h"
#include "MinHook.h"
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <unordered_map>
#include <string>

extern "C" void Log(const char* fmt, ...);

// ================================================================
// Texture Load Request Structure
// ================================================================
struct TextureRequest {
    char filename[260];   // Texture filename
    void* callbackData;   // Callback data for completion
    int priority;         // Load priority (0 = normal, 1 = high)
};

// ================================================================
// Lock-Free Queue (8192 entries, ring buffer)
// ================================================================
static constexpr int QUEUE_SIZE = 8192;
static constexpr int QUEUE_MASK = QUEUE_SIZE - 1;

struct QueueEntry {
    TextureRequest data;
    volatile LONG ready;  // 1 = ready to process, 0 = empty
};

static QueueEntry g_queue[QUEUE_SIZE] = {};
static volatile LONG g_queueHead = 0;  // Consumer index (worker threads)
static volatile LONG g_queueTail = 0;  // Producer index (main thread)

// ================================================================
// Texture Cache (LRU cache for loaded textures)
// ================================================================
static constexpr int CACHE_SIZE = 2048;
static std::unordered_map<std::string, void*> g_textureCache;
static SRWLOCK g_cacheLock = SRWLOCK_INIT;

// ================================================================
// Statistics (atomic counters)
// ================================================================
static volatile LONG g_requestsQueued = 0;
static volatile LONG g_requestsCompleted = 0;
static volatile LONG g_requestsDropped = 0;
static volatile LONG g_cacheHits = 0;
static volatile LONG g_cacheMisses = 0;
static double g_totalLoadTimeMs = 0.0;
static SRWLOCK g_loadTimeLock = SRWLOCK_INIT;

// ================================================================
// Worker Thread Pool State
// ================================================================
static constexpr int WORKER_THREAD_COUNT = 2;
static HANDLE g_workerThreads[WORKER_THREAD_COUNT] = {};
static volatile bool g_workerShutdown = false;
static HANDLE g_workerEvent = NULL;
static double g_qpcFreqMs = 0.0;

// ================================================================
// Hook State
// ================================================================
typedef void (__cdecl *LoadTexture_fn)(int, char*);
static LoadTexture_fn orig_LoadTexture = nullptr;
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
// Texture Loading (Worker Thread)
// ================================================================
static void LoadTextureAsync(const TextureRequest* request) {
    // Check cache first
    AcquireSRWLockShared(&g_cacheLock);
    auto it = g_textureCache.find(request->filename);
    bool cached = (it != g_textureCache.end());
    ReleaseSRWLockShared(&g_cacheLock);

    if (cached) {
        InterlockedIncrement(&g_cacheHits);
        InterlockedIncrement(&g_requestsCompleted);
        return;
    }

    InterlockedIncrement(&g_cacheMisses);

    // Load texture from disk (call original function)
    // For now, we just simulate the load - in production this would
    // call the actual WoW texture loading code
    
    // TODO: Implement actual texture loading via WoW's file I/O
    // This requires calling into WoW's MPQ reading functions
    
    // Add to cache
    AcquireSRWLockExclusive(&g_cacheLock);
    if (g_textureCache.size() < CACHE_SIZE) {
        g_textureCache[request->filename] = nullptr; // Placeholder
    }
    ReleaseSRWLockExclusive(&g_cacheLock);

    InterlockedIncrement(&g_requestsCompleted);
}

// ================================================================
// Worker Thread Procedure
// ================================================================
static DWORD WINAPI WorkerThreadProc(LPVOID) {
    Log("[TextureAsync] Worker thread started (TID: %d)", GetCurrentThreadId());

    while (!g_workerShutdown) {
        // Wait for events (1ms timeout to check shutdown flag)
        WaitForSingleObject(g_workerEvent, 1);

        // Process all available requests
        LONG head = g_queueHead;
        LONG tail = InterlockedCompareExchange(&g_queueTail, 0, 0); // Read tail atomically

        if (head == tail) {
            continue; // Queue empty
        }

        while (head != tail) {
            int slot = head & QUEUE_MASK;
            QueueEntry* entry = &g_queue[slot];

            if (entry->ready) {
                LARGE_INTEGER start, end;
                QueryPerformanceCounter(&start);

                LoadTextureAsync(&entry->data);

                QueryPerformanceCounter(&end);
                double loadTimeMs = (double)(end.QuadPart - start.QuadPart) / g_qpcFreqMs;

                AcquireSRWLockExclusive(&g_loadTimeLock);
                g_totalLoadTimeMs += loadTimeMs;
                ReleaseSRWLockExclusive(&g_loadTimeLock);

                InterlockedExchange(&entry->ready, 0);
            }

            head = (head + 1) & 0x7FFFFFFF; // Prevent overflow
            InterlockedExchange(&g_queueHead, head);
        }
    }

    Log("[TextureAsync] Worker thread exiting");
    return 0;
}

// ================================================================
// Hooked Function: sub_619330 (Texture Loading)
// ================================================================
static void __cdecl Hooked_LoadTexture(int a1, char* filename) {
    // Check cache first on main thread
    if (filename && *filename) {
        AcquireSRWLockShared(&g_cacheLock);
        auto it = g_textureCache.find(filename);
        bool cached = (it != g_textureCache.end());
        ReleaseSRWLockShared(&g_cacheLock);

        if (cached) {
            InterlockedIncrement(&g_cacheHits);
            // Return cached texture immediately
            return;
        }
    }

    // Validate filename before queuing
    if (!filename || !*filename) {
        orig_LoadTexture(a1, filename);
        return;
    }

    // Copy request data to queue
    LONG tail = InterlockedIncrement(&g_queueTail) - 1;
    int slot = tail & QUEUE_MASK;

    QueueEntry* queueEntry = &g_queue[slot];

    // Check if slot is still being processed (queue overflow)
    if (queueEntry->ready) {
        InterlockedIncrement(&g_requestsDropped);
        // Fall back to synchronous load
        orig_LoadTexture(a1, filename);
        return;
    }

    // Copy texture request data
    strncpy_s(queueEntry->data.filename, sizeof(queueEntry->data.filename), 
              filename, _TRUNCATE);
    queueEntry->data.callbackData = (void*)(uintptr_t)a1;
    queueEntry->data.priority = 0;
    
    InterlockedExchange(&queueEntry->ready, 1);
    InterlockedIncrement(&g_requestsQueued);

    // Signal worker threads
    SetEvent(g_workerEvent);
}

// ================================================================
// Public API Implementation
// ================================================================
namespace TextureAsync {

bool Init() {
    Log("[TextureAsync] Init (build 12340)");

    // Initialize QPC frequency
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpcFreqMs = (double)freq.QuadPart / 1000.0;

    // Validate target address (sub_619330 - texture loading)
    uintptr_t targetAddr = 0x00619330;
    if (!IsExecutable(targetAddr)) {
        Log("[TextureAsync] ERROR: Target address 0x%08X is not executable", targetAddr);
        return false;
    }

    // Create worker event
    g_workerEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!g_workerEvent) {
        Log("[TextureAsync] ERROR: Failed to create worker event");
        return false;
    }

    // Create worker thread pool
    g_workerShutdown = false;
    for (int i = 0; i < WORKER_THREAD_COUNT; i++) {
        g_workerThreads[i] = CreateThread(NULL, 0, WorkerThreadProc, NULL, 0, NULL);
        if (!g_workerThreads[i]) {
            Log("[TextureAsync] ERROR: Failed to create worker thread %d", i);
            Shutdown();
            return false;
        }
        // Set worker thread priority
        SetThreadPriority(g_workerThreads[i], THREAD_PRIORITY_BELOW_NORMAL);
    }

    // Install hook
    void* target = (void*)targetAddr;
    if (MH_CreateHook(target, (void*)Hooked_LoadTexture, (void**)&orig_LoadTexture) != MH_OK) {
        Log("[TextureAsync] ERROR: Failed to create hook");
        Shutdown();
        return false;
    }

    if (MH_EnableHook(target) != MH_OK) {
        Log("[TextureAsync] ERROR: Failed to enable hook");
        MH_RemoveHook(target);
        Shutdown();
        return false;
    }

    g_initialized = true;
    Log("[TextureAsync] [ OK ] Hook installed at 0x%08X (texture loader)", targetAddr);
    Log("[TextureAsync] [ OK ] Worker thread pool created (%d threads, queue size: %d)", 
        WORKER_THREAD_COUNT, QUEUE_SIZE);
    return true;
}

void Shutdown() {
    if (!g_initialized) return;

    Log("[TextureAsync] Shutdown");

    // Signal worker threads to exit
    g_workerShutdown = true;
    if (g_workerEvent) SetEvent(g_workerEvent);

    // Wait for worker threads (5 second timeout each)
    for (int i = 0; i < WORKER_THREAD_COUNT; i++) {
        if (g_workerThreads[i]) {
            DWORD waitResult = WaitForSingleObject(g_workerThreads[i], 5000);
            if (waitResult == WAIT_TIMEOUT) {
                Log("[TextureAsync] WARNING: Worker thread %d did not exit, terminating", i);
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

    // Clear cache
    AcquireSRWLockExclusive(&g_cacheLock);
    g_textureCache.clear();
    ReleaseSRWLockExclusive(&g_cacheLock);

    // Remove hook
    MH_DisableHook((void*)0x00619330);
    MH_RemoveHook((void*)0x00619330);

    // Log final stats
    Log("[TextureAsync] Final stats: Queued=%d, Completed=%d, Dropped=%d, CacheHits=%d, CacheMisses=%d",
        g_requestsQueued, g_requestsCompleted, g_requestsDropped, g_cacheHits, g_cacheMisses);

    g_initialized = false;
}

void OnFrame(DWORD mainThreadId) {
    if (!g_initialized) return;
    if (GetCurrentThreadId() != mainThreadId) return;

    // Update queue depth stat
    LONG head = g_queueHead;
    LONG tail = g_queueTail;
    LONG depth = (tail - head) & 0x7FFFFFFF;
    if (depth > QUEUE_SIZE) depth = QUEUE_SIZE;
}

Stats GetStats() {
    Stats s;
    s.requestsQueued = g_requestsQueued;
    s.requestsCompleted = g_requestsCompleted;
    s.requestsDropped = g_requestsDropped;
    s.cacheHits = g_cacheHits;
    s.cacheMisses = g_cacheMisses;
    
    LONG head = g_queueHead;
    LONG tail = g_queueTail;
    LONG depth = (tail - head) & 0x7FFFFFFF;
    if (depth > QUEUE_SIZE) depth = QUEUE_SIZE;
    s.queueDepth = depth;

    AcquireSRWLockShared(&g_loadTimeLock);
    s.totalLoadTimeMs = g_totalLoadTimeMs;
    ReleaseSRWLockShared(&g_loadTimeLock);

    return s;
}

} // namespace TextureAsync
