// Async Model/M2 Loader — background MPQ prefetch
// Hooks sub_81C390. Worker threads read model files ahead of WoW.
//
// Approach: cannot safely cache model pointers (WoW frees/reuses).
// Instead, prefetch model file data into OS cache via background workers.
// First load = disk I/O (unavoidable). Repeat load = OS cache hit (instant).

#include "model_async.h"
#include "lua_optimize.h"
#include "MinHook.h"
#include <cstdio>
#include <cstring>

extern "C" void Log(const char* fmt, ...);

static constexpr int QUEUE_SIZE  = 512;
static constexpr int QUEUE_MASK  = QUEUE_SIZE - 1;
static constexpr int CACHE_SIZE  = 1024;
static constexpr int PREFETCH_BUF = 256 * 1024;  // 256KB per file

struct PrefetchTask {
    char filename[260];
    volatile LONG status;  // 0=empty, 1=pending, 2=done
};

static PrefetchTask g_queue[QUEUE_SIZE] = {};
static volatile LONG g_queueHead = 0;
static volatile LONG g_queueTail = 0;

// Simple LRU: store filenames we've already prefetched.
static char g_cache[CACHE_SIZE][260] = {};
static long g_cacheCount = 0;
static SRWLOCK g_cacheLock = SRWLOCK_INIT;

static bool IsInCache(const char* name) {
    AcquireSRWLockShared(&g_cacheLock);
    for (int i = 0; i < g_cacheCount; i++) {
        if (_stricmp(g_cache[i], name) == 0) {
            ReleaseSRWLockShared(&g_cacheLock);
            return true;
        }
    }
    ReleaseSRWLockShared(&g_cacheLock);
    return false;
}

static void AddToCache(const char* name) {
    AcquireSRWLockExclusive(&g_cacheLock);
    if (g_cacheCount < CACHE_SIZE) {
        strncpy(g_cache[g_cacheCount++], name, 259);
    }
    ReleaseSRWLockExclusive(&g_cacheLock);
}

static void ClearCache() {
    AcquireSRWLockExclusive(&g_cacheLock);
    g_cacheCount = 0;
    ReleaseSRWLockExclusive(&g_cacheLock);
}

// Workers
static HANDLE g_workers[2] = {};
static volatile bool g_shutdown = false;
static uint8_t* g_readBuf = nullptr;

static DWORD WINAPI WorkerProc(LPVOID) {
    while (!g_shutdown) {
        if (LuaOpt::IsSwapping()) { Sleep(1); continue; }

        LONG head = g_queueHead;
        if (head == g_queueTail) { SwitchToThread(); continue; }

        PrefetchTask& t = g_queue[head];
        if (t.status == 1) {
            // Open the file and read it to populate OS cache
            HANDLE h = CreateFileA(t.filename, GENERIC_READ,
                FILE_SHARE_READ, NULL, OPEN_EXISTING,
                FILE_FLAG_SEQUENTIAL_SCAN, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                DWORD read = 0;
                while (ReadFile(h, g_readBuf, PREFETCH_BUF, &read, NULL) && read > 0) {}
                CloseHandle(h);
            }
            t.status = 2;
        }
        InterlockedExchange(&g_queueHead, (head + 1) & QUEUE_MASK);
    }
    return 0;
}

static void QueuePrefetch(const char* filename) {
    LONG tail = g_queueTail;
    LONG next = (tail + 1) & QUEUE_MASK;
    if (next == g_queueHead) return;  // full

    strncpy(g_queue[tail].filename, filename, 259);
    g_queue[tail].status = 1;
    InterlockedExchange(&g_queueTail, next);
}

// Hook
typedef int (__thiscall *LoadModel_fn)(void*, void*, unsigned int);
static LoadModel_fn orig_LoadModel = nullptr;
static bool g_init = false;

static volatile LONG g_hits = 0, g_misses = 0;

static int __fastcall Hooked_LoadModel(void* This, void* unused, void* block, unsigned int flags) {
    if (LuaOpt::IsReloading() || LuaOpt::IsSwapping())
        return orig_LoadModel(This, block, flags);

    if (!block || !*(char*)block)
        return orig_LoadModel(This, block, flags);

    const char* filename = (const char*)block;

    // Call original synchronously (we can't bypass model parsing)
    int result = orig_LoadModel(This, block, flags);

    // If this model isn't in cache, queue background prefetch so
    // the NEXT load of this model hits OS cache instead of disk.
    if (!IsInCache(filename)) {
        AddToCache(filename);
        QueuePrefetch(filename);
        InterlockedIncrement(&g_misses);
    } else {
        InterlockedIncrement(&g_hits);
    }

    return result;
}

namespace ModelAsync {

bool Init() {
    g_shutdown = false;
    g_readBuf = new uint8_t[PREFETCH_BUF];
    if (!g_readBuf) { Log("[ModelAsync] alloc failed"); return false; }

    // Start 2 background workers
    for (int i = 0; i < 2; i++) {
        g_workers[i] = CreateThread(NULL, 0, WorkerProc, NULL, 0, NULL);
        if (g_workers[i])
            SetThreadPriority(g_workers[i], THREAD_PRIORITY_BELOW_NORMAL);
    }

    void* target = (void*)0x0081C390;
    if (MH_CreateHook(target, (void*)Hooked_LoadModel, (void**)&orig_LoadModel) != MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        Log("[ModelAsync] Hook failed");
        return false;
    }

    g_init = true;
    Log("[ModelAsync] Hook at 0x0081C390, 2 workers, %d-slot prefetch queue", QUEUE_SIZE);
    return true;
}

void Shutdown() {
    if (!g_init) return;
    g_shutdown = true;
    for (int i = 0; i < 2; i++) {
        if (g_workers[i]) {
            WaitForSingleObject(g_workers[i], 3000);
            CloseHandle(g_workers[i]);
            g_workers[i] = NULL;
        }
    }
    delete[] g_readBuf;
    g_readBuf = nullptr;
    ClearCache();
    MH_DisableHook((void*)0x0081C390);
    MH_RemoveHook((void*)0x0081C390);
    Log("[ModelAsync] Shutdown: hits=%d misses=%d", g_hits, g_misses);
    g_init = false;
}

void OnFrame(DWORD mainThreadId) {
    if (!g_init || GetCurrentThreadId() != mainThreadId) return;
}

Stats GetStats() {
    Stats s = {};
    s.cacheHits = g_hits;
    s.cacheMisses = g_misses;
    s.queueDepth = (g_queueTail - g_queueHead) & 0x7FFFFFFF;
    return s;
}

} // namespace ModelAsync
