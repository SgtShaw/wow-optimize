// Async Texture Loader — background MPQ prefetch
// Hooks sub_619330. Worker threads pre-read texture files into OS cache.
// On cache miss: queue prefetch. On cache hit: return instantly.
// WoW loads textures asynchronously, so we just ensure data is in RAM.

#include "texture_async.h"
#include "lua_optimize.h"
#include "MinHook.h"
#include <cstdio>
#include <cstring>

extern "C" void Log(const char* fmt, ...);

static constexpr int QUEUE_SIZE  = 8192;
static constexpr int QUEUE_MASK  = QUEUE_SIZE - 1;
static constexpr int CACHE_SIZE  = 2048;
static constexpr int PREFETCH_BUF = 256 * 1024;

struct PrefetchTask {
    char filename[260];
    volatile LONG status;
};

static PrefetchTask g_queue[QUEUE_SIZE] = {};
static volatile LONG g_queueHead = 0;
static volatile LONG g_queueTail = 0;

static char g_cache[CACHE_SIZE][260] = {};
static long g_cacheCount = 0;
static SRWLOCK g_cacheLock = SRWLOCK_INIT;

static bool IsCached(const char* name) {
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

static void AddCached(const char* name) {
    AcquireSRWLockExclusive(&g_cacheLock);
    if (g_cacheCount < CACHE_SIZE)
        strncpy(g_cache[g_cacheCount++], name, 259);
    ReleaseSRWLockExclusive(&g_cacheLock);
}

static void ClearCache() {
    AcquireSRWLockExclusive(&g_cacheLock);
    g_cacheCount = 0;
    ReleaseSRWLockExclusive(&g_cacheLock);
}

// Workers
static constexpr int WORKER_COUNT = 2;
static HANDLE g_workers[WORKER_COUNT] = {};
static volatile bool g_shutdown = false;
static uint8_t* g_readBuf = nullptr;
static HANDLE g_wakeEvent = NULL;

static DWORD WINAPI WorkerProc(LPVOID) {
    while (!g_shutdown) {
        if (LuaOpt::IsSwapping()) { Sleep(1); continue; }

        LONG head = g_queueHead;
        if (head == g_queueTail) {
            WaitForSingleObject(g_wakeEvent, 1);
            continue;
        }

        PrefetchTask& t = g_queue[head];
        if (t.status == 1) {
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
    if (next == g_queueHead) return;
    strncpy(g_queue[tail].filename, filename, 259);
    g_queue[tail].status = 1;
    InterlockedExchange(&g_queueTail, next);
    SetEvent(g_wakeEvent);
}

// Hook
typedef void (__cdecl *LoadTexture_fn)(int, char*);
static LoadTexture_fn orig_LoadTexture = nullptr;
static bool g_init = false;
static volatile LONG g_hits = 0, g_misses = 0;

static void __cdecl Hooked_LoadTexture(int a1, char* filename) {
    if (LuaOpt::IsReloading() || LuaOpt::IsSwapping()) {
        orig_LoadTexture(a1, filename);
        return;
    }

    if (!filename || !*filename) {
        orig_LoadTexture(a1, filename);
        return;
    }

    // Always call original — we can't bypass WoW's internal texture pipeline.
    orig_LoadTexture(a1, filename);

    // Queue background prefetch so next load hits OS cache.
    if (!IsCached(filename)) {
        AddCached(filename);
        QueuePrefetch(filename);
        InterlockedIncrement(&g_misses);
    } else {
        InterlockedIncrement(&g_hits);
    }
}

namespace TextureAsync {

bool Init() {
    g_shutdown = false;
    g_readBuf = new uint8_t[PREFETCH_BUF];
    if (!g_readBuf) { Log("[TextureAsync] alloc failed"); return false; }

    g_wakeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!g_wakeEvent) { delete[] g_readBuf; return false; }

    for (int i = 0; i < WORKER_COUNT; i++) {
        g_workers[i] = CreateThread(NULL, 0, WorkerProc, NULL, 0, NULL);
        if (g_workers[i])
            SetThreadPriority(g_workers[i], THREAD_PRIORITY_BELOW_NORMAL);
    }

    void* target = (void*)0x00619330;
    if (MH_CreateHook(target, (void*)Hooked_LoadTexture, (void**)&orig_LoadTexture) != MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        Log("[TextureAsync] Hook failed");
        return false;
    }

    g_init = true;
    Log("[TextureAsync] Hook at 0x619330, 2 workers, %d-slot prefetch queue", QUEUE_SIZE);
    return true;
}

void Shutdown() {
    if (!g_init) return;
    g_shutdown = true;
    if (g_wakeEvent) SetEvent(g_wakeEvent);
    for (int i = 0; i < WORKER_COUNT; i++) {
        if (g_workers[i]) {
            WaitForSingleObject(g_workers[i], 3000);
            CloseHandle(g_workers[i]);
            g_workers[i] = NULL;
        }
    }
    delete[] g_readBuf;
    g_readBuf = nullptr;
    if (g_wakeEvent) { CloseHandle(g_wakeEvent); g_wakeEvent = NULL; }
    ClearCache();
    MH_DisableHook((void*)0x00619330);
    MH_RemoveHook((void*)0x00619330);
    Log("[TextureAsync] Shutdown: hits=%d misses=%d", g_hits, g_misses);
    g_init = false;
}

void OnFrame(DWORD mainThreadId) {}

Stats GetStats() {
    Stats s = {};
    s.cacheHits = g_hits;
    s.cacheMisses = g_misses;
    s.queueDepth = (g_queueTail - g_queueHead) & 0x7FFFFFFF;
    return s;
}

} // namespace TextureAsync
