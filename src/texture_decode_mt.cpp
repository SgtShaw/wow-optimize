#include "texture_decode_mt.h"
#include "lua_optimize.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace TextureDecodeMT {

static const int    WORKER_COUNT = 2;
static const size_t QUEUE_SIZE   = 128;
static const size_t QUEUE_MASK   = QUEUE_SIZE - 1;

struct Job {
    const uint8_t* src;
    uint32_t       srcSize;
    uint8_t*       dst;
    uint32_t       dstCap;
    HANDLE         done;
    volatile LONG  ready;
};
static Job           g_queue[QUEUE_SIZE] = {};
static volatile LONG g_queueHead = 0;
static volatile LONG g_queueTail = 0;

static volatile LONG  g_active = 0;
static volatile LONG  g_stop   = 0;
static HANDLE         g_workers[WORKER_COUNT] = {};
static HANDLE         g_wakeup = nullptr;
static volatile LONG64 g_observed  = 0;
static volatile LONG64 g_fallbacks = 0;

bool SubmitDecode(const uint8_t* src, uint32_t sz,
                  uint8_t* dst, uint32_t dstCap, HANDLE done) {
    if (!g_active) return false;
    LONG head = g_queueHead;
    LONG next = (head + 1) & (LONG)QUEUE_MASK;
    if (next == g_queueTail) {
        InterlockedIncrement64(&g_fallbacks);
        return false;
    }
    g_queue[head].src     = src;
    g_queue[head].srcSize = sz;
    g_queue[head].dst     = dst;
    g_queue[head].dstCap  = dstCap;
    g_queue[head].done    = done;
    InterlockedExchange(&g_queue[head].ready, 1);
    InterlockedExchange(&g_queueHead, next);
    SetEvent(g_wakeup);
    return true;
}

static DWORD WINAPI Worker(LPVOID) {
    while (!g_stop) {
        // Pause during UI reload or lua_State swap to prevent accessing stale data
        if (LuaOpt::IsReloading() || LuaOpt::IsSwapping()) {
            Sleep(1);
            continue;
        }

        WaitForSingleObject(g_wakeup, 200);
        for (;;) {
            LONG tail = g_queueTail;
            if (tail == g_queueHead || !g_queue[tail].ready) break;

            HANDLE done = g_queue[tail].done;

            // BLP decode requires palette/DXT walking - kept dormant
            // until the BLP loader entry is hooked. Caller falls back.
            InterlockedIncrement64(&g_fallbacks);
            if (done) SetEvent(done);

            InterlockedExchange(&g_queue[tail].ready, 0);
            InterlockedExchange(&g_queueTail, (tail + 1) & (LONG)QUEUE_MASK);
        }
    }
    return 0;
}

bool Init() {
    g_wakeup = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!g_wakeup) return false;
    for (int i = 0; i < WORKER_COUNT; i++) {
        g_workers[i] = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
        if (g_workers[i]) SetThreadPriority(g_workers[i], THREAD_PRIORITY_BELOW_NORMAL);
    }
    InterlockedExchange(&g_active, 1);
    Log("[TextureDecodeMT] active workers=%d queue=%u (BLP path dormant)",
        WORKER_COUNT, (unsigned)QUEUE_SIZE);
    return true;
}

void Shutdown() {
    InterlockedExchange(&g_active, 0);
    InterlockedExchange(&g_stop, 1);
    if (g_wakeup) for (int i = 0; i < WORKER_COUNT; i++) SetEvent(g_wakeup);
    for (int i = 0; i < WORKER_COUNT; i++) {
        if (!g_workers[i]) continue;
        if (WaitForSingleObject(g_workers[i], 2000) == WAIT_TIMEOUT)
            TerminateThread(g_workers[i], 0);
        CloseHandle(g_workers[i]);
        g_workers[i] = nullptr;
    }
    if (g_wakeup) { CloseHandle(g_wakeup); g_wakeup = nullptr; }
    Log("[TextureDecodeMT] shutdown observed=%lld fallbacks=%lld",
       (long long)g_observed, (long long)g_fallbacks);
}

void OnFrame() {}

void GetStats(Stats* out) {
    if (!out) return;
    out->active         = g_active != 0;
    out->workers        = WORKER_COUNT;
    out->framesObserved = (uint64_t)g_observed;
    out->fallbacks      = (uint64_t)g_fallbacks;
    LONG h = g_queueHead, t = g_queueTail;
    out->queueDepth = (uint32_t)((h - t) & (LONG)QUEUE_MASK);
}

} // namespace TextureDecodeMT
