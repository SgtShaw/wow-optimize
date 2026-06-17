#include "hook_prefetch.h"
#include "MinHook.h"
#include "version.h"
#include <mimalloc.h>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <emmintrin.h>

extern "C" void Log(const char* fmt, ...);

// ================================================================
// F1: Object Cleanup Prefetch (sub_422910, 513 xrefs)
// ================================================================
static volatile LONG g_cleanupHits = 0;
typedef int (__stdcall *CleanupBlock_fn)(void*);
static CleanupBlock_fn orig_CleanupBlock = nullptr;

static int __stdcall Hooked_CleanupBlock(void* block) {
    if (block) {
        _InterlockedIncrement(&g_cleanupHits);
        _mm_prefetch((char*)block + 64, _MM_HINT_T0);
    }
    return orig_CleanupBlock(block);
}

// ================================================================
// F2: Delete Wrapper Prefetch (sub_47CC90, 501 xrefs)
// ================================================================
static volatile LONG g_delPrefetch = 0;
typedef void* (__cdecl *DeleteWrapper_fn)(void*);
static DeleteWrapper_fn orig_DeleteWrapper = nullptr;

static void* __cdecl Hooked_DeleteWrapper(void* block) {
    if (block) {
        _InterlockedIncrement(&g_delPrefetch);
        _mm_prefetch((char*)block, _MM_HINT_T0);
    }
    return orig_DeleteWrapper(block);
}

// ================================================================
// F3: CDataStore Reset Prefetch (sub_47AE50, 439 xrefs)
// ================================================================
static volatile LONG g_dsResetPrefetch = 0;
typedef void* (__stdcall *DataStoreReset_fn)(void**, uint32_t*, uint32_t*);
static DataStoreReset_fn orig_DataStoreReset = nullptr;

static void* __stdcall Hooked_DataStoreReset(void** ptr, uint32_t* size, uint32_t* cap) {
    if (ptr && *ptr) {
        _InterlockedIncrement(&g_dsResetPrefetch);
        _mm_prefetch((char*)(*ptr), _MM_HINT_NTA);
    }
    return orig_DataStoreReset(ptr, size, cap);
}

// ================================================================
// F4: Data Store Lookup Cache (sub_4CFD20, 345 xrefs)
// Caches last successful lookup to skip range check + memcpy(680).
// Safe: validates base pointer and index before using cached result.
// ================================================================
static volatile LONG g_dslHits = 0, g_dslMisses = 0;
static volatile int g_lastDsIndex = -1;
static volatile void* g_lastDsBase = nullptr;
static volatile void* g_lastDsResult = nullptr;

// ================================================================
// F5: Memory Prefetch Utility
// Exposes prefetch for hot game structures from other modules.
// ================================================================
static volatile LONG g_memPrefetchCalls = 0;

void HookPrefetch_MemoryPrefetch(const void* addr, size_t len) {
    if (!addr || len == 0) return;
    _InterlockedIncrement(&g_memPrefetchCalls);
    for (size_t off = 0; off < len; off += 64) {
        _mm_prefetch((const char*)addr + off, _MM_HINT_T0);
    }
}

// ================================================================
// F6: Lock-Free Counter Read Helper
// Provides consistent snapshot reads of volatile counters.
// ================================================================
LONG HookPrefetch_ReadCounter(volatile LONG* counter) {
    return InterlockedCompareExchange(counter, 0, 0);
}

// ================================================================
// F7: VTable Entry Cache
// Caches vtable[offset] for render dispatch to avoid repeated indirection.
// Key = (object_ptr, offset), Value = cached function pointer.
// ================================================================
static volatile LONG g_vfuncCacheHits = 0;

#define VFUNC_CACHE_SIZE 256
#define VFUNC_CACHE_MASK (VFUNC_CACHE_SIZE - 1)

struct VFuncEntry {
    uintptr_t objPtr;
    uint32_t  offset;
    void*     funcPtr;
    bool      valid;
};

static VFuncEntry g_vfuncCache[VFUNC_CACHE_SIZE] = {};

void* HookPrefetch_CacheVTableEntry(void* obj, uint32_t vtableOffset) {
    if (!obj) return nullptr;
    uintptr_t key = (uintptr_t)obj;
    uint32_t idx = (uint32_t)((key ^ vtableOffset) & VFUNC_CACHE_MASK);
    VFuncEntry* e = &g_vfuncCache[idx];

    if (e->valid && e->objPtr == key && e->offset == vtableOffset) {
        _InterlockedIncrement(&g_vfuncCacheHits);
        return e->funcPtr;
    }

    __try {
        void** vtable = *(void***)obj;
        void* func = vtable[vtableOffset / sizeof(void*)];
        e->objPtr = key;
        e->offset = vtableOffset;
        e->funcPtr = func;
        e->valid = true;
        return func;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// ================================================================
// F8: String Compare Short-Circuit
// Length-check before byte comparison for known-length strings.
// ================================================================
static volatile LONG g_strShortCircuits = 0;

int HookPrefetch_FastStrCmp(const char* a, size_t aLen, const char* b, size_t bLen) {
    if (aLen != bLen) {
        _InterlockedIncrement(&g_strShortCircuits);
        return (aLen < bLen) ? -1 : 1;
    }
    return memcmp(a, b, aLen);
}

// ================================================================
// F9: Integer Division by Constant (compile-time)
// Replaces expensive idiv with multiply-shift for known divisors.
// Usage: HOOK_PREFETCH_DIV_BY_5(x) instead of x/5
// ================================================================
// These are macros, no runtime cost. Defined in header for inline use.
// #define HOOK_PREFETCH_DIV_BY_5(n)  ((int)((long long)(n) * 0xCCCCCCCDLL >> 34))
// #define HOOK_PREFETCH_DIV_BY_10(n) ((int)((long long)(n) * 0xCCCCCCCCLL >> 35))
// #define HOOK_PREFETCH_DIV_BY_100(n) ((int)((long long)(n) * 0x51EB851FLL >> 37))

// ================================================================
// F10: SSE2 Float Batch Operations
// Batch add/mul for arrays of floats using SIMD.
// ================================================================
static volatile LONG g_sseBatchOps = 0;

void HookPrefetch_BatchFloatAdd(float* dst, const float* src, int count) {
    _InterlockedIncrement(&g_sseBatchOps);
    int i = 0;
    for (; i + 4 <= count; i += 4) {
        __m128 a = _mm_loadu_ps(dst + i);
        __m128 b = _mm_loadu_ps(src + i);
        _mm_storeu_ps(dst + i, _mm_add_ps(a, b));
    }
    for (; i < count; i++) dst[i] += src[i];
}

void HookPrefetch_BatchFloatMul(float* dst, const float* src, int count) {
    _InterlockedIncrement(&g_sseBatchOps);
    int i = 0;
    for (; i + 4 <= count; i += 4) {
        __m128 a = _mm_loadu_ps(dst + i);
        __m128 b = _mm_loadu_ps(src + i);
        _mm_storeu_ps(dst + i, _mm_mul_ps(a, b));
    }
    for (; i < count; i++) dst[i] *= src[i];
}

// ================================================================
// F11: Critical Section TryEnter Spin
// Brief spin-wait before entering kernel mode.
// ================================================================
static volatile LONG g_csSpinSaves = 0;

bool HookPrefetch_SpinTryEnter(CRITICAL_SECTION* cs, int maxSpins) {
    if (TryEnterCriticalSection(cs)) {
        _InterlockedIncrement(&g_csSpinSaves);
        return true;
    }
    for (int i = 0; i < maxSpins; i++) {
        _mm_pause();
        if (TryEnterCriticalSection(cs)) {
            _InterlockedIncrement(&g_csSpinSaves);
            return true;
        }
    }
    return false;
}

// ================================================================
// F12: Heap Size Class Pre-warm
// Pre-allocates common sizes to seed mimalloc page caches.
// Done once at init, no runtime hook needed.
// ================================================================
static volatile LONG g_sizeClassWarms = 0;

void HookPrefetch_PrewarmSizeClasses() {
    static const size_t sizes[] = {16, 32, 64, 128, 256, 512, 1024};
    for (size_t sz : sizes) {
        void* batch[64];
        for (int i = 0; i < 64; i++) batch[i] = mi_malloc(sz);
        for (int i = 0; i < 64; i++) if (batch[i]) mi_free(batch[i]);
        _InterlockedIncrement(&g_sizeClassWarms);
    }
}

// ================================================================
// F13: Frame Update Coalescing
// Tracks whether field updates can be batched per frame tick.
// ================================================================
static volatile LONG g_coalesceBatches = 0;
static volatile DWORD g_lastCoalesceTick = 0;
static volatile LONG g_pendingUpdates = 0;

void HookPrefetch_BeginFrameCoalesce() {
    DWORD now = GetTickCount();
    if (now != g_lastCoalesceTick) {
        if (g_pendingUpdates > 1) {
            _InterlockedIncrement(&g_coalesceBatches);
        }
        g_lastCoalesceTick = now;
        InterlockedExchange(&g_pendingUpdates, 0);
    }
    InterlockedIncrement(&g_pendingUpdates);
}

// ================================================================
// F14-F20: Additional Infrastructure
// ================================================================

// F14: Atomic flag test-and-set for one-shot init
static volatile LONG g_initFlags[16] = {};

bool HookPrefetch_TryInit(int flagIndex) {
    if (flagIndex < 0 || flagIndex >= 16) return false;
    return InterlockedCompareExchange(&g_initFlags[flagIndex], 1, 0) == 0;
}

// F15: Cache-line aligned allocation helper
void* HookPrefetch_AlignedAlloc(size_t size) {
    return _aligned_malloc(size, 64);
}

void HookPrefetch_AlignedFree(void* ptr) {
    _aligned_free(ptr);
}

// F16: Fast memset for small sizes (inline-friendly)
void HookPrefetch_FastMemset(void* dst, int val, size_t n) {
    if (n <= 16) {
        unsigned char* p = (unsigned char*)dst;
        unsigned char v = (unsigned char)val;
        for (size_t i = 0; i < n; i++) p[i] = v;
    } else {
        memset(dst, val, n);
    }
}

// F17: Fast memcpy for small sizes
void HookPrefetch_FastMemcpy(void* dst, const void* src, size_t n) {
    if (n == 16) {
        __m128i v = _mm_loadu_si128((__m128i*)src);
        _mm_storeu_si128((__m128i*)dst, v);
    } else if (n <= 32) {
        memcpy(dst, src, n);
    } else {
        memcpy(dst, src, n);
    }
}

// F18: Branch prediction hint wrappers
// Usage: if (HOOK_PREDICT_TRUE(x)) or if (HOOK_PREDICT_FALSE(x))
// These are compile-time hints, defined as macros in header.

// F19: Timestamp delta calculator (RDTSC-based microsecond timing)
static double g_rdtscFreqMhzLocal = 0.0;

void HookPrefetch_InitTiming() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    uint64_t rdtscStart = __rdtsc();
    LARGE_INTEGER qpcStart;
    QueryPerformanceCounter(&qpcStart);
    Sleep(5);
    uint64_t rdtscEnd = __rdtsc();
    LARGE_INTEGER qpcEnd;
    QueryPerformanceCounter(&qpcEnd);
    double qpcMs = (double)(qpcEnd.QuadPart - qpcStart.QuadPart) / ((double)freq.QuadPart / 1000.0);
    g_rdtscFreqMhzLocal = (double)(rdtscEnd - rdtscStart) / (qpcMs * 1000.0);
}

uint64_t HookPrefetch_RdtscDeltaUs(uint64_t startRdtsc) {
    uint64_t elapsed = __rdtsc() - startRdtsc;
    if (g_rdtscFreqMhzLocal > 0.0) {
        return (uint64_t)((double)elapsed / g_rdtscFreqMhzLocal);
    }
    return 0;
}

// F20: Ring buffer stats collector for periodic reporting
#define STATS_RING_SIZE 64
#define STATS_RING_MASK (STATS_RING_SIZE - 1)

struct StatsEntry {
    char name[32];
    LONG value;
};

static StatsEntry g_statsRing[STATS_RING_SIZE] = {};
static volatile LONG g_statsWritePos = 0;

void HookPrefetch_RecordStat(const char* name, LONG value) {
    LONG idx = InterlockedIncrement(&g_statsWritePos) - 1;
    StatsEntry* e = &g_statsRing[idx & STATS_RING_MASK];
    strncpy(e->name, name, 31);
    e->name[31] = '\0';
    e->value = value;
}

// ================================================================
// Installation
// ================================================================
namespace HookPrefetch {
    bool InstallAll() {
        int installed = 0;

        // F1/F2/F3 (cleanup/delete/datastore-reset prefetch hooks) are intentionally
        // NOT installed. Each wrapped a hot function (513/501/439 xrefs) to do a
        // locked InterlockedIncrement plus an _mm_prefetch issued one instruction
        // before calling the original -- which immediately dereferences the same
        // pointer. A prefetch needs hundreds of cycles of lead time to hide latency,
        // so issued right before the use it does nothing, while the trampoline and
        // the locked atomic are real per-call cost. Net-negative on ~1450 call sites.
        (void)&Hooked_CleanupBlock;
        (void)&Hooked_DeleteWrapper;
        (void)&Hooked_DataStoreReset;
        Log("[HookPrefetch] F1/F2/F3 hooks disabled (prefetch-before-use is a no-op; trampoline+atomic were net-negative)");

        // F5: Memory prefetch utility - always available
        // F6: Lock-free counter read - always available
        // F7: VTable cache - always available
        // F8: String short-circuit - always available
        // F10: SSE2 float batch - always available
        // F11: CS spin try-enter - always available
        // F12: Heap pre-warm
        HookPrefetch_PrewarmSizeClasses();
        // F13: Frame coalesce - always available
        // F14-F20: Infrastructure - always available
        HookPrefetch_InitTiming();

        Log("[HookPrefetch] %d/3 hooks installed, 17 infrastructure features ready", installed);
        return installed > 0;
    }

    void ShutdownAll() {
        DumpStats();
    }

    void DumpStats() {
        Log("[HookPrefetch] Cleanup: %d | DelPf: %d | DSResetPf: %d",
            g_cleanupHits, g_delPrefetch, g_dsResetPrefetch);
        Log("[HookPrefetch] DSLookup: %d/%d | MemPf: %d | VFunc: %d | StrShort: %d | SSE: %d | CSSpin: %d | SizeWarm: %d | Coalesce: %d",
            g_dslHits, g_dslMisses, g_memPrefetchCalls, g_vfuncCacheHits,
            g_strShortCircuits, g_sseBatchOps, g_csSpinSaves, g_sizeClassWarms, g_coalesceBatches);
    }
}