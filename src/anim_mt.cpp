#include "anim_mt.h"
#include "lua_optimize.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace AnimMT {

static const int    WORKER_COUNT = 2;
static const size_t QUEUE_SIZE   = 64;
static const size_t QUEUE_MASK   = QUEUE_SIZE - 1;

struct Job {
    void*         m2;
    uint32_t      bones;
    HANDLE        done;
    volatile LONG ready;
};
static Job           g_queue[QUEUE_SIZE] = {};
static volatile LONG g_queueHead = 0;
static volatile LONG g_queueTail = 0;

static volatile LONG  g_active = 0;
static volatile LONG  g_stop   = 0;
static HANDLE         g_workers[WORKER_COUNT] = {};
static HANDLE         g_wakeup = nullptr;
static volatile LONG64 g_batches   = 0;
static volatile LONG64 g_fallbacks = 0;

bool SubmitBones(void* m2, uint32_t bones, HANDLE done) {
    if (!g_active) return false;
    LONG head = g_queueHead;
    LONG next = (head + 1) & (LONG)QUEUE_MASK;
    if (next == g_queueTail) {
        InterlockedIncrement64(&g_fallbacks);
        return false;
    }
    g_queue[head].m2    = m2;
    g_queue[head].bones = bones;
    g_queue[head].done  = done;
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

            // M2 bone walk requires verified instance layout offsets.
            // Held until the animation hook is wired; falls back to
            // synchronous on the caller side.
            InterlockedIncrement64(&g_fallbacks);
            if (done) SetEvent(done);

            InterlockedExchange(&g_queue[tail].ready, 0);
            InterlockedExchange(&g_queueTail, (tail + 1) & (LONG)QUEUE_MASK);
        }
    }
    return 0;
}

bool Init() {
#if TEST_DISABLE_ANIM_MT
    Log("[AnimMT] DISABLED (dormant: no producer ever submits bone work)");
    return false;
#endif
    g_wakeup = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!g_wakeup) return false;
    for (int i = 0; i < WORKER_COUNT; i++) {
        g_workers[i] = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
        if (g_workers[i]) SetThreadPriority(g_workers[i], THREAD_PRIORITY_BELOW_NORMAL);
    }
    InterlockedExchange(&g_active, 1);
    Log("[AnimMT] active workers=%d queue=%u (M2 bone path dormant)",
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
    Log("[AnimMT] shutdown batches=%lld fallbacks=%lld",
       (long long)g_batches, (long long)g_fallbacks);
}

void OnFrame() {}

void GetStats(Stats* out) {
    if (!out) return;
    out->active    = g_active != 0;
    out->workers   = WORKER_COUNT;
    out->batches   = (uint64_t)g_batches;
    out->fallbacks = (uint64_t)g_fallbacks;
    LONG h = g_queueHead, t = g_queueTail;
    out->queueDepth = (uint32_t)((h - t) & (LONG)QUEUE_MASK);
}

} // namespace AnimMT
