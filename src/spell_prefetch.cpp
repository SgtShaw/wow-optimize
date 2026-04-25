// ================================================================
// Async Spell Data Prefetcher — Implementation
// WoW 3.3.5a build 12340
// ================================================================

#include "spell_prefetch.h"
#include "MinHook.h"
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <unordered_map>

extern "C" void Log(const char* fmt, ...);

// ================================================================
// Spell Data Structure (680 bytes from IDA analysis)
// ================================================================
struct SpellData {
    char data[680];  // 0x2A8 bytes
};

// ================================================================
// Spell Prefetch Request Structure
// ================================================================
struct PrefetchRequest {
    int spellID;           // Spell ID to prefetch
    int priority;          // Prefetch priority (0 = normal, 1 = high)
};

// ================================================================
// Lock-Free Queue (4096 entries, ring buffer)
// ================================================================
static constexpr int QUEUE_SIZE = 4096;
static constexpr int QUEUE_MASK = QUEUE_SIZE - 1;

struct QueueEntry {
    PrefetchRequest data;
    volatile LONG ready;  // 1 = ready to process, 0 = empty
};

static QueueEntry g_queue[QUEUE_SIZE] = {};
static volatile LONG g_queueHead = 0;  // Consumer index (worker thread)
static volatile LONG g_queueTail = 0;  // Producer index (main thread)

// ================================================================
// Spell Data Cache (LRU cache for prefetched spell data)
// ================================================================
static constexpr int CACHE_SIZE = 4096;
static std::unordered_map<int, SpellData> g_spellCache;
static SRWLOCK g_cacheLock = SRWLOCK_INIT;

// ================================================================
// Statistics (atomic counters)
// ================================================================
static volatile LONG g_requestsQueued = 0;
static volatile LONG g_requestsCompleted = 0;
static volatile LONG g_requestsDropped = 0;
static volatile LONG g_cacheHits = 0;
static volatile LONG g_cacheMisses = 0;
static double g_totalPrefetchTimeMs = 0.0;
static SRWLOCK g_prefetchTimeLock = SRWLOCK_INIT;

// ================================================================
// Worker Thread State
// ================================================================
static HANDLE g_workerThread = NULL;
static volatile bool g_workerShutdown = false;
static HANDLE g_workerEvent = NULL;
static double g_qpcFreqMs = 0.0;

// ================================================================
// Hook State
// ================================================================
typedef int (__cdecl *CastSpell_fn)(int, int, int, int, int, int, int);
typedef int (__thiscall *LoadSpellData_fn)(void*, int, void*);

static CastSpell_fn orig_CastSpell = nullptr;
static LoadSpellData_fn orig_LoadSpellData = nullptr;
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
// Spell Data Prefetching (Worker Thread)
// ================================================================
static void PrefetchSpellData(const PrefetchRequest* request) {
    // Check cache first
    AcquireSRWLockShared(&g_cacheLock);
    auto it = g_spellCache.find(request->spellID);
    bool cached = (it != g_spellCache.end());
    ReleaseSRWLockShared(&g_cacheLock);

    if (cached) {
        InterlockedIncrement(&g_cacheHits);
        InterlockedIncrement(&g_requestsCompleted);
        return;
    }

    InterlockedIncrement(&g_cacheMisses);

    // Load spell data from WoW's spell data cache
    // Call the original spell data loader (sub_4CFD20)
    SpellData spellData;
    
    // Get the spell data manager pointer (this is a global in WoW)
    // For now, we'll just allocate placeholder data
    // TODO: Call actual WoW spell data loader via orig_LoadSpellData
    
    memset(&spellData, 0, sizeof(SpellData));
    
    // Add to cache
    AcquireSRWLockExclusive(&g_cacheLock);
    if (g_spellCache.size() < CACHE_SIZE) {
        g_spellCache[request->spellID] = spellData;
    } else {
        // LRU eviction - remove first entry (simple approach)
        auto it = g_spellCache.begin();
        g_spellCache.erase(it);
        g_spellCache[request->spellID] = spellData;
    }
    ReleaseSRWLockExclusive(&g_cacheLock);

    InterlockedIncrement(&g_requestsCompleted);
}

// ================================================================
// Worker Thread Procedure
// ================================================================
static DWORD WINAPI WorkerThreadProc(LPVOID) {
    Log("[SpellPrefetch] Worker thread started (TID: %d)", GetCurrentThreadId());

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

                PrefetchSpellData(&entry->data);

                QueryPerformanceCounter(&end);
                double prefetchTimeMs = (double)(end.QuadPart - start.QuadPart) / g_qpcFreqMs;

                AcquireSRWLockExclusive(&g_prefetchTimeLock);
                g_totalPrefetchTimeMs += prefetchTimeMs;
                ReleaseSRWLockExclusive(&g_prefetchTimeLock);

                InterlockedExchange(&entry->ready, 0);
            }

            head = (head + 1) & 0x7FFFFFFF; // Prevent overflow
            InterlockedExchange(&g_queueHead, head);
        }
    }

    Log("[SpellPrefetch] Worker thread exiting");
    return 0;
}

// ================================================================
// Hooked Function: sub_80CCE0 (Spell Cast)
// ================================================================
static int __cdecl Hooked_CastSpell(int a1, int a2, int a3, int spellID, int a5, int a6, int a7) {
    // Queue spell data prefetch before cast completes
    if (spellID > 0) {
        // Check cache first on main thread
        AcquireSRWLockShared(&g_cacheLock);
        auto it = g_spellCache.find(spellID);
        bool cached = (it != g_spellCache.end());
        ReleaseSRWLockShared(&g_cacheLock);

        if (cached) {
            InterlockedIncrement(&g_cacheHits);
        } else {
            // Queue for async prefetch
            LONG tail = InterlockedIncrement(&g_queueTail) - 1;
            int slot = tail & QUEUE_MASK;

            QueueEntry* queueEntry = &g_queue[slot];

            // Check if slot is still being processed (queue overflow)
            if (!queueEntry->ready) {
                // Copy prefetch request data
                queueEntry->data.spellID = spellID;
                queueEntry->data.priority = 0;
                
                InterlockedExchange(&queueEntry->ready, 1);
                InterlockedIncrement(&g_requestsQueued);

                // Signal worker thread
                SetEvent(g_workerEvent);
            } else {
                InterlockedIncrement(&g_requestsDropped);
            }
        }
    }

    // Call original cast function
    return orig_CastSpell(a1, a2, a3, spellID, a5, a6, a7);
}

// ================================================================
// Public API Implementation
// ================================================================
namespace SpellPrefetch {

bool Init() {
    Log("[SpellPrefetch] Init (build 12340)");

    // Initialize QPC frequency
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpcFreqMs = (double)freq.QuadPart / 1000.0;

    // Validate target address (sub_80CCE0 - spell cast)
    uintptr_t targetAddr = 0x0080CCE0;
    if (!IsExecutable(targetAddr)) {
        Log("[SpellPrefetch] ERROR: Target address 0x%08X is not executable", targetAddr);
        return false;
    }

    // Create worker event
    g_workerEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!g_workerEvent) {
        Log("[SpellPrefetch] ERROR: Failed to create worker event");
        return false;
    }

    // Create worker thread
    g_workerShutdown = false;
    g_workerThread = CreateThread(NULL, 0, WorkerThreadProc, NULL, 0, NULL);
    if (!g_workerThread) {
        Log("[SpellPrefetch] ERROR: Failed to create worker thread");
        CloseHandle(g_workerEvent);
        g_workerEvent = NULL;
        return false;
    }

    // Set worker thread priority
    SetThreadPriority(g_workerThread, THREAD_PRIORITY_BELOW_NORMAL);

    // Install hook
    void* target = (void*)targetAddr;
    if (MH_CreateHook(target, (void*)Hooked_CastSpell, (void**)&orig_CastSpell) != MH_OK) {
        Log("[SpellPrefetch] ERROR: Failed to create hook");
        Shutdown();
        return false;
    }

    if (MH_EnableHook(target) != MH_OK) {
        Log("[SpellPrefetch] ERROR: Failed to enable hook");
        MH_RemoveHook(target);
        Shutdown();
        return false;
    }

    g_initialized = true;
    Log("[SpellPrefetch] [ OK ] Hook installed at 0x%08X (spell cast)", targetAddr);
    Log("[SpellPrefetch] [ OK ] Worker thread created (queue size: %d, cache size: %d)", 
        QUEUE_SIZE, CACHE_SIZE);
    return true;
}

void Shutdown() {
    if (!g_initialized) return;

    Log("[SpellPrefetch] Shutdown");

    // Signal worker thread to exit
    g_workerShutdown = true;
    if (g_workerEvent) SetEvent(g_workerEvent);

    // Wait for worker thread (5 second timeout)
    if (g_workerThread) {
        DWORD waitResult = WaitForSingleObject(g_workerThread, 5000);
        if (waitResult == WAIT_TIMEOUT) {
            Log("[SpellPrefetch] WARNING: Worker thread did not exit, terminating");
            TerminateThread(g_workerThread, 1);
        }
        CloseHandle(g_workerThread);
        g_workerThread = NULL;
    }

    // Cleanup event
    if (g_workerEvent) {
        CloseHandle(g_workerEvent);
        g_workerEvent = NULL;
    }

    // Clear cache
    AcquireSRWLockExclusive(&g_cacheLock);
    g_spellCache.clear();
    ReleaseSRWLockExclusive(&g_cacheLock);

    // Remove hook
    MH_DisableHook((void*)0x0080CCE0);
    MH_RemoveHook((void*)0x0080CCE0);

    // Log final stats
    Log("[SpellPrefetch] Final stats: Queued=%d, Completed=%d, Dropped=%d, CacheHits=%d, CacheMisses=%d",
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

    AcquireSRWLockShared(&g_prefetchTimeLock);
    s.totalPrefetchTimeMs = g_totalPrefetchTimeMs;
    ReleaseSRWLockShared(&g_prefetchTimeLock);

    return s;
}

} // namespace SpellPrefetch
