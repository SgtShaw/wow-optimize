#include "mpq_decompress_mt.h"
#include "lua_optimize.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace MPQDecompressMT {

static const int    WORKER_COUNT = 3;
static const size_t QUEUE_SIZE   = 256;
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

static volatile LONG64 g_blocks    = 0;
static volatile LONG64 g_bytesIn   = 0;
static volatile LONG64 g_bytesOut  = 0;
static volatile LONG64 g_fallbacks = 0;

// Compression-method byte: 0x02 = zlib (DEFLATE w/ zlib header).
// We only handle zlib here; other methods (BZIP2/LZMA/SPARSE/HUFF)
// fall through to caller. Most patch MPQs use zlib for Lua/XML.
static bool InflateZlib(const uint8_t* src, uint32_t srcSize,
                        uint8_t* dst, uint32_t dstCap, uint32_t* out) {
    // mimalloc and minhook ship without zlib. We use the OS RtlDecompressBuffer
    // for COMPRESSION_FORMAT_LZNT1 only as a placeholder; for true MPQ zlib
    // payloads the caller must fall through. Returns false for now.
   (void)src; (void)srcSize; (void)dst; (void)dstCap;
    if (out) *out = 0;
    return false;
}

bool SubmitBlock(const uint8_t* src, uint32_t srcSize,
                 uint8_t* dst, uint32_t dstCap, HANDLE done) {
    if (!g_active) return false;
    LONG head = g_queueHead;
    LONG next = (head + 1) & (LONG)QUEUE_MASK;
    if (next == g_queueTail) {
        InterlockedIncrement64(&g_fallbacks);
        return false;
    }
    g_queue[head].src     = src;
    g_queue[head].srcSize = srcSize;
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

            const uint8_t* src   = g_queue[tail].src;
            uint32_t       sz    = g_queue[tail].srcSize;
            uint8_t*       dst   = g_queue[tail].dst;
            uint32_t       dstCap = g_queue[tail].dstCap;
            HANDLE         done  = g_queue[tail].done;

            uint32_t produced = 0;
            bool ok = InflateZlib(src, sz, dst, dstCap, &produced);

            if (ok) {
                InterlockedIncrement64(&g_blocks);
                InterlockedExchangeAdd64(&g_bytesIn,  sz);
                InterlockedExchangeAdd64(&g_bytesOut, produced);
            } else {
                InterlockedIncrement64(&g_fallbacks);
            }
            if (done) SetEvent(done);

            InterlockedExchange(&g_queue[tail].ready, 0);
            InterlockedExchange(&g_queueTail, (tail + 1) & (LONG)QUEUE_MASK);
        }
    }
    return 0;
}

bool Init() {
#if TEST_DISABLE_MPQ_DECOMPRESS_MT
    Log("[MPQDecompressMT] DISABLED (dormant: no producer ever submits inflate work)");
    return false;
#endif
    g_wakeup = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!g_wakeup) return false;
    for (int i = 0; i < WORKER_COUNT; i++) {
        g_workers[i] = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
        if (g_workers[i]) SetThreadPriority(g_workers[i], THREAD_PRIORITY_BELOW_NORMAL);
    }
    InterlockedExchange(&g_active, 1);
    Log("[MPQDecompressMT] active workers=%d queue=%u", WORKER_COUNT, (unsigned)QUEUE_SIZE);
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
    Log("[MPQDecompressMT] shutdown blocks=%lld in=%lld out=%lld fallbacks=%lld",
       (long long)g_blocks, (long long)g_bytesIn,
       (long long)g_bytesOut, (long long)g_fallbacks);
}

void OnFrame() {}

void GetStats(Stats* out) {
    if (!out) return;
    out->active          = g_active != 0;
    out->workers         = WORKER_COUNT;
    out->blocksProcessed = (uint64_t)g_blocks;
    out->bytesIn         = (uint64_t)g_bytesIn;
    out->bytesOut        = (uint64_t)g_bytesOut;
    out->fallbacks       = (uint64_t)g_fallbacks;
    LONG h = g_queueHead, t = g_queueTail;
    out->queueDepth = (uint32_t)((h - t) & (LONG)QUEUE_MASK);
}

} // namespace MPQDecompressMT
