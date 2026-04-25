// ================================================================
// Multithreaded Addon Update Dispatcher — Implementation
// WoW 3.3.5a build 12340
// ================================================================

#include "addon_dispatcher.h"
#include "MinHook.h"
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <vector>

extern "C" void Log(const char* fmt, ...);

// ================================================================
// Addon Callback Structure
// ================================================================
struct AddonCallback {
    void* frame;          // Frame pointer
    void* callback;       // Callback function pointer
    double elapsed;       // Elapsed time since last call
    int priority;         // Callback priority (0 = normal, 1 = high)
};

// ================================================================
// Lock-Free Queue (8192 entries, ring buffer)
// ================================================================
static constexpr int QUEUE_SIZE = 8192;
static constexpr int QUEUE_MASK = QUEUE_SIZE - 1;

struct QueueEntry {
    AddonCallback data;
    volatile LONG ready;  // 1 = ready to process, 0 = empty
};

static QueueEntry g_queue[QUEUE_SIZE] = {};
static volatile LONG g_queueHead = 0;  // Consumer index (worker threads)
static volatile LONG g_queueTail = 0;  // Producer index (main thread)

// ================================================================
// Batch Processing State
// ================================================================
static std::vector<AddonCallback> g_currentBatch;
static SRWLOCK g_batchLock = SRWLOCK_INIT;

// ================================================================
// Statistics (atomic counters)
// ================================================================
static volatile LONG g_callbacksQueued = 0;
static volatile LONG g_callbacksProcessed = 0;
static volatile LONG g_callbacksDropped = 0;
static volatile LONG g_batchesProcessed = 0;
static double g_totalProcessTimeMs = 0.0;
static double g_totalBatchSize = 0.0;
static SRWLOCK g_statsLock = SRWLOCK_INIT;

// ================================================================
// Worker Thread Pool State
// ================================================================
static constexpr int WORKER_THREAD_COUNT = 4;
static HANDLE g_workerThreads[WORKER_THREAD_COUNT] = {};
static volatile bool g_workerShutdown = false;
static HANDLE g_workerEvent = NULL;
static double g_qpcFreqMs = 0.0;

// ================================================================
// Hook State
// ================================================================
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

// ================================================================
// Callback Processing (Worker Thread)
// ================================================================
static void ProcessCallback(const AddonCallback* callback) {
    // Validate pointers before calling
    if (!IsReadable((uintptr_t)callback->frame) || 
        !IsReadable((uintptr_t)callback->callback)) {
        return;
    }

    // Call the addon callback
    // For now, this is a placeholder - in production this would
    // call the actual Lua callback function
    // TODO: Implement actual Lua callback execution
    
    InterlockedIncrement(&g_callbacksProcessed);
}

// ================================================================
// Worker Thread Procedure
// ================================================================
static DWORD WINAPI WorkerThreadProc(LPVOID) {
    Log("[AddonDispatcher] Worker thread started (TID: %d)", GetCurrentThreadId());

    while (!g_workerShutdown) {
        // Wait for events (1ms timeout to check shutdown flag)
        WaitForSingleObject(g_workerEvent, 1);

        // Process all available callbacks
        LONG head = g_queueHead;
        LONG tail = InterlockedCompareExchange(&g_queueTail, 0, 0); // Read tail atomically

        if (head == tail) {
            continue; // Queue empty
        }

        while (head != tail) {
            int slot = head & QUEUE_MASK;
            QueueEntry* entry = &g_queue[slot];

            if (entry->ready) {
                ProcessCallback(&entry->data);
                InterlockedExchange(&entry->ready, 0);
            }

            head = (head + 1) & 0x7FFFFFFF; // Prevent overflow
            InterlockedExchange(&g_queueHead, head);
        }
    }

    Log("[AddonDispatcher] Worker thread exiting");
    return 0;
}

// ================================================================
// Batch Collection and Dispatch
// ================================================================
static void CollectCallback(void* frame, void* callback, double elapsed) {
    if (!g_initialized) return;

    // Add to current batch
    AcquireSRWLockExclusive(&g_batchLock);
    
    AddonCallback cb;
    cb.frame = frame;
    cb.callback = callback;
    cb.elapsed = elapsed;
    cb.priority = 0;
    
    g_currentBatch.push_back(cb);
    
    ReleaseSRWLockExclusive(&g_batchLock);
}

static void DispatchBatch() {
    if (!g_initialized) return;

    AcquireSRWLockExclusive(&g_batchLock);
    
    if (g_currentBatch.empty()) {
        ReleaseSRWLockExclusive(&g_batchLock);
        return;
    }

    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);

    // Queue all callbacks from batch
    for (const auto& callback : g_currentBatch) {
        LONG tail = InterlockedIncrement(&g_queueTail) - 1;
        int slot = tail & QUEUE_MASK;

        QueueEntry* queueEntry = &g_queue[slot];

        // Check if slot is still being processed (queue overflow)
        if (!queueEntry->ready) {
            queueEntry->data = callback;
            InterlockedExchange(&queueEntry->ready, 1);
            InterlockedIncrement(&g_callbacksQueued);
        } else {
            InterlockedIncrement(&g_callbacksDropped);
        }
    }

    // Signal worker threads
    SetEvent(g_workerEvent);

    QueryPerformanceCounter(&end);
    double processTimeMs = (double)(end.QuadPart - start.QuadPart) / g_qpcFreqMs;

    // Update stats
    AcquireSRWLockExclusive(&g_statsLock);
    g_totalProcessTimeMs += processTimeMs;
    g_totalBatchSize += (double)g_currentBatch.size();
    ReleaseSRWLockExclusive(&g_statsLock);

    InterlockedIncrement(&g_batchesProcessed);

    // Clear batch
    g_currentBatch.clear();
    
    ReleaseSRWLockExclusive(&g_batchLock);
}

// ================================================================
// Public API Implementation
// ================================================================
namespace AddonDispatcher {

bool Init() {
    Log("[AddonDispatcher] Init (build 12340)");

    // Initialize QPC frequency
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpcFreqMs = (double)freq.QuadPart / 1000.0;

    // Create worker event
    g_workerEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!g_workerEvent) {
        Log("[AddonDispatcher] ERROR: Failed to create worker event");
        return false;
    }

    // Create worker thread pool
    g_workerShutdown = false;
    for (int i = 0; i < WORKER_THREAD_COUNT; i++) {
        g_workerThreads[i] = CreateThread(NULL, 0, WorkerThreadProc, NULL, 0, NULL);
        if (!g_workerThreads[i]) {
            Log("[AddonDispatcher] ERROR: Failed to create worker thread %d", i);
            Shutdown();
            return false;
        }
        // Set worker thread priority
        SetThreadPriority(g_workerThreads[i], THREAD_PRIORITY_BELOW_NORMAL);
    }

    g_initialized = true;
    Log("[AddonDispatcher] [ OK ] Worker thread pool created (%d threads, queue size: %d)", 
        WORKER_THREAD_COUNT, QUEUE_SIZE);
    return true;
}

void Shutdown() {
    if (!g_initialized) return;

    Log("[AddonDispatcher] Shutdown");

    // Signal worker threads to exit
    g_workerShutdown = true;
    if (g_workerEvent) SetEvent(g_workerEvent);

    // Wait for worker threads (5 second timeout each)
    for (int i = 0; i < WORKER_THREAD_COUNT; i++) {
        if (g_workerThreads[i]) {
            DWORD waitResult = WaitForSingleObject(g_workerThreads[i], 5000);
            if (waitResult == WAIT_TIMEOUT) {
                Log("[AddonDispatcher] WARNING: Worker thread %d did not exit, terminating", i);
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

    // Clear batch
    AcquireSRWLockExclusive(&g_batchLock);
    g_currentBatch.clear();
    ReleaseSRWLockExclusive(&g_batchLock);

    // Log final stats
    Log("[AddonDispatcher] Final stats: Queued=%d, Processed=%d, Dropped=%d, Batches=%d",
        g_callbacksQueued, g_callbacksProcessed, g_callbacksDropped, g_batchesProcessed);

    g_initialized = false;
}

void OnFrame(DWORD mainThreadId) {
    if (!g_initialized) return;
    if (GetCurrentThreadId() != mainThreadId) return;

    // Dispatch current batch at end of frame
    DispatchBatch();
}

Stats GetStats() {
    Stats s;
    s.callbacksQueued = g_callbacksQueued;
    s.callbacksProcessed = g_callbacksProcessed;
    s.callbacksDropped = g_callbacksDropped;
    s.batchesProcessed = g_batchesProcessed;
    
    LONG head = g_queueHead;
    LONG tail = g_queueTail;
    LONG depth = (tail - head) & 0x7FFFFFFF;
    if (depth > QUEUE_SIZE) depth = QUEUE_SIZE;
    s.queueDepth = depth;

    AcquireSRWLockShared(&g_statsLock);
    s.totalProcessTimeMs = g_totalProcessTimeMs;
    s.avgBatchSize = (g_batchesProcessed > 0) ? 
        (g_totalBatchSize / (double)g_batchesProcessed) : 0.0;
    ReleaseSRWLockShared(&g_statsLock);

    return s;
}

} // namespace AddonDispatcher
