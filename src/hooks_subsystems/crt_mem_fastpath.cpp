// ============================================================================
// Module: crt_mem_fastpath.cpp
// Description: SSE2 vectorized replacement for legacy CRT function `crt_mem_fastpath.cpp`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================

#include <windows.h>
#include <intrin.h>
#include <emmintrin.h>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

// Stats (defined in dllmain.cpp)
extern long g_crtStrlenHits, g_crtStrlenFallbacks;
extern long g_crtStrcmpHits, g_crtStrcmpFallbacks;
extern long g_crtMemcmpHits, g_crtMemcmpFallbacks;
extern long g_crtMemcpyHits, g_crtMemcpyFallbacks;
extern long g_crtMemsetHits, g_crtMemsetFallbacks;

// Init-readiness gate (set after all originals are captured).
// Prevents hooks from firing during MinHook's own install sequence
// or during early CRT initialization before originals are valid.
static volatile LONG g_crtReady = 0;

// Thread-local recursion guard.
// __declspec(thread) is only accessible by the owning thread, so no
// cross-thread atomic (InterlockedExchange) is needed. A plain volatile
// read/write is correct and eliminates the lock-prefixed instruction
// overhead on every strlen/memcpy/memset call.
static __declspec(thread) volatile long g_crtInHook = 0;

#define CRT_ENTER() do { \
    if (g_crtInHook) goto fallback; \
    g_crtInHook = 1; \
} while(0)
#define CRT_LEAVE() (g_crtInHook = 0)

// Page-safety check: returns true if a 16-byte SSE load/store at ptr
// would cross a 4KB page boundary into a potentially unmapped page.
// ptr must be at page offset <= 0xFF0 for a safe 16-byte access.
#define PAGE_NEAR_BOUNDARY(p) (((uintptr_t)(p) & 0xFFF) > 0xFF0)

// Originals
typedef size_t (__cdecl* strlen_fn)(const char*);
typedef int   (__cdecl* strcmp_fn)(const char*, const char*);
typedef int   (__cdecl* memcmp_fn)(const void*, const void*, size_t);
typedef void* (__cdecl* memcpy_fn)(void*, const void*, size_t);
typedef void* (__cdecl* memset_fn)(void*, int, size_t);
typedef int (__cdecl* strncmp_fn)(const char*, const char*, size_t);

static strlen_fn  orig_strlen  = nullptr;
static strcmp_fn  orig_strcmp  = nullptr;
static memcmp_fn  orig_memcmp  = nullptr;
static memcpy_fn  orig_memcpy  = nullptr;
static memset_fn  orig_memset  = nullptr;
static strncmp_fn orig_strncmp = nullptr;

// ================================================================
// strlen - SSE2 null-terminator scan with in-loop page boundary guard
// ================================================================
static size_t __cdecl hooked_strlen(const char* s) {
    if (!g_crtReady || !orig_strlen) goto fallback;
    CRT_ENTER();
    if (!s) { CRT_LEAVE(); goto fallback; }
    __try {
        const __m128i zero = _mm_setzero_si128();
        size_t len = 0;
        while (true) {
            // Page-boundary guard inside the loop
            if (PAGE_NEAR_BOUNDARY(s + len)) {
                CRT_LEAVE();
                goto fallback;
            }
            __m128i v = _mm_loadu_si128((const __m128i*)(s + len));
            int mask = _mm_movemask_epi8(_mm_cmpeq_epi8(v, zero));
            if (mask) {
                unsigned long idx;
                _BitScanForward(&idx, (unsigned long)mask);
                len += idx;
                CRT_LEAVE();
                g_crtStrlenHits++;
                return len;
            }
            len += 16;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { CRT_LEAVE(); }
fallback:
    g_crtStrlenFallbacks++;
    return orig_strlen ? orig_strlen(s) : 0;
}

// ================================================================
// strcmp - SSE2 byte-wise compare with termination-index checking
// ================================================================
static int __cdecl hooked_strcmp(const char* s1, const char* s2) {
    if (!g_crtReady || !orig_strcmp) goto fallback;
    CRT_ENTER();
    if (!s1 || !s2) { CRT_LEAVE(); goto fallback; }
    __try {
        const __m128i zero = _mm_setzero_si128();
        size_t i = 0;
        while (true) {
            // Page-boundary guard inside the loop
            if (PAGE_NEAR_BOUNDARY(s1 + i) || PAGE_NEAR_BOUNDARY(s2 + i)) {
                CRT_LEAVE();
                goto fallback;
            }
            __m128i a = _mm_loadu_si128((const __m128i*)(s1 + i));
            __m128i b = _mm_loadu_si128((const __m128i*)(s2 + i));

            int diff = _mm_movemask_epi8(_mm_cmpeq_epi8(a, b));
            int nullA = _mm_movemask_epi8(_mm_cmpeq_epi8(a, zero));
            int nullB = _mm_movemask_epi8(_mm_cmpeq_epi8(b, zero));

            unsigned long diff_idx = 16;
            if (diff != 0xFFFF) {
                _BitScanForward(&diff_idx, (unsigned long)(~diff & 0xFFFF));
            }

            unsigned long null_idx = 16;
            int nullMask = nullA | nullB;
            if (nullMask != 0) {
                _BitScanForward(&null_idx, (unsigned long)nullMask);
            }

            if (diff_idx <= null_idx) {
                int result = (unsigned char)s1[i + diff_idx] - (unsigned char)s2[i + diff_idx];
                CRT_LEAVE();
                g_crtStrcmpHits++;
                return result;
            }

            if (nullMask != 0) {
                CRT_LEAVE();
                g_crtStrcmpHits++;
                return 0;
            }
            i += 16;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { CRT_LEAVE(); }
fallback:
    g_crtStrcmpFallbacks++;
    return orig_strcmp ? orig_strcmp(s1, s2) : 0;
}

// ================================================================
// strncmp - SSE2 byte-wise compare with size limit n
// ================================================================
static int __cdecl hooked_strncmp(const char* s1, const char* s2, size_t n) {
    if (!g_crtReady || !orig_strncmp) goto fallback;
    CRT_ENTER();
    if (!s1 || !s2 || n == 0) { CRT_LEAVE(); goto fallback; }
    __try {
        const __m128i zero = _mm_setzero_si128();
        size_t i = 0;
        while (i + 16 <= n) {
            // Page-boundary guard inside the loop
            if (PAGE_NEAR_BOUNDARY(s1 + i) || PAGE_NEAR_BOUNDARY(s2 + i)) {
                CRT_LEAVE();
                goto fallback;
            }
            __m128i a = _mm_loadu_si128((const __m128i*)(s1 + i));
            __m128i b = _mm_loadu_si128((const __m128i*)(s2 + i));

            int diff = _mm_movemask_epi8(_mm_cmpeq_epi8(a, b));
            int nullA = _mm_movemask_epi8(_mm_cmpeq_epi8(a, zero));
            int nullB = _mm_movemask_epi8(_mm_cmpeq_epi8(b, zero));

            unsigned long diff_idx = 16;
            if (diff != 0xFFFF) {
                _BitScanForward(&diff_idx, (unsigned long)(~diff & 0xFFFF));
            }

            unsigned long null_idx = 16;
            int nullMask = nullA | nullB;
            if (nullMask != 0) {
                _BitScanForward(&null_idx, (unsigned long)nullMask);
            }

            if (diff_idx <= null_idx) {
                int result = (unsigned char)s1[i + diff_idx] - (unsigned char)s2[i + diff_idx];
                CRT_LEAVE();
                return result;
            }

            if (nullMask != 0) {
                CRT_LEAVE();
                return 0;
            }
            i += 16;
        }
        while (i < n) {
            int d = (unsigned char)s1[i] - (unsigned char)s2[i];
            if (d) { CRT_LEAVE(); return d; }
            if (!s1[i]) { CRT_LEAVE(); return 0; }
            i++;
        }
        CRT_LEAVE();
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { CRT_LEAVE(); }
fallback:
    return orig_strncmp ? orig_strncmp(s1, s2, n) : 0;
}

// ================================================================
// memcmp - SSE2 vectorized comparison with per-chunk page guard
// ================================================================
static int __cdecl hooked_memcmp(const void* s1, const void* s2, size_t n) {
    if (!g_crtReady || !orig_memcmp) goto fallback;
    CRT_ENTER();
    if (!s1 || !s2 || n == 0) { CRT_LEAVE(); goto fallback; }
    __try {
        const unsigned char* p1 = (const unsigned char*)s1;
        const unsigned char* p2 = (const unsigned char*)s2;
        size_t i = 0;
        for (; i + 16 <= n; i += 16) {
            // Per-chunk page-boundary guard (was missing — caused
            // ACCESS_VIOLATION when a long compare crossed into an
            // unmapped page mid-loop)
            if (PAGE_NEAR_BOUNDARY(p1 + i) || PAGE_NEAR_BOUNDARY(p2 + i)) {
                CRT_LEAVE();
                goto fallback;
            }
            __m128i a = _mm_loadu_si128((const __m128i*)(p1 + i));
            __m128i b = _mm_loadu_si128((const __m128i*)(p2 + i));
            int eq = _mm_movemask_epi8(_mm_cmpeq_epi8(a, b));
            if (eq != 0xFFFF) {
                unsigned long idx;
                _BitScanForward(&idx, (unsigned long)(~eq & 0xFFFF));
                int result = p1[i + idx] - p2[i + idx];
                CRT_LEAVE();
                g_crtMemcmpHits++;
                return result;
            }
        }
        for (; i < n; i++) {
            int diff = p1[i] - p2[i];
            if (diff) { CRT_LEAVE(); g_crtMemcmpHits++; return diff; }
        }
        CRT_LEAVE();
        g_crtMemcmpHits++;
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { CRT_LEAVE(); }
fallback:
    g_crtMemcmpFallbacks++;
    return orig_memcmp ? orig_memcmp(s1, s2, n) : 0;
}

// ================================================================
// memcpy - SSE2 unaligned copy with per-chunk page guard
// ================================================================
static void* __cdecl hooked_memcpy(void* dst, const void* src, size_t n) {
    if (!g_crtReady || !orig_memcpy) goto fallback;
    CRT_ENTER();
    if (!dst || !src || n == 0) { CRT_LEAVE(); goto fallback; }
    // Reject overlapping copies — fall back to original (which may
    // use memmove semantics). Only check the forward-overlap case
    // since memcpy with overlap is UB, but the original may handle it.
    if (src < dst && (const char*)src + n > (char*)dst) { CRT_LEAVE(); goto fallback; }
    __try {
        unsigned char* d = (unsigned char*)dst;
        const unsigned char* s = (const unsigned char*)src;
        size_t i = 0;
        for (; i + 16 <= n; i += 16) {
            // Per-chunk page-boundary guard for BOTH load and store.
            // The old code only checked the start address, so a copy
            // longer than 16 bytes could cross a page boundary mid-loop
            // and fault on _mm_loadu_si128 / _mm_storeu_si128.
            if (PAGE_NEAR_BOUNDARY(s + i) || PAGE_NEAR_BOUNDARY(d + i)) {
                CRT_LEAVE();
                goto fallback;
            }
            _mm_storeu_si128((__m128i*)(d + i), _mm_loadu_si128((const __m128i*)(s + i)));
        }
        for (size_t j = i; j < n; j++) d[j] = s[j];

        CRT_LEAVE();
        g_crtMemcpyHits++;
        return dst;
    } __except(EXCEPTION_EXECUTE_HANDLER) { CRT_LEAVE(); }
fallback:
    g_crtMemcpyFallbacks++;
    return orig_memcpy ? orig_memcpy(dst, src, n) : dst;
}

// ================================================================
// memset - SSE2 byte broadcast with per-chunk page guard
// ================================================================
static void* __cdecl hooked_memset(void* dst, int c, size_t n) {
    if (!g_crtReady || !orig_memset) goto fallback;
    CRT_ENTER();
    if (!dst || n == 0) { CRT_LEAVE(); goto fallback; }
    __try {
        __m128i val = _mm_set1_epi8((char)c);
        unsigned char* d = (unsigned char*)dst;
        size_t i = 0;
        for (; i + 16 <= n; i += 16) {
            // Per-chunk page-boundary guard for the store.
            // The old code only checked the start address, so a fill
            // longer than 16 bytes could cross a page boundary mid-loop.
            if (PAGE_NEAR_BOUNDARY(d + i)) {
                CRT_LEAVE();
                goto fallback;
            }
            _mm_storeu_si128((__m128i*)(d + i), val);
        }
        for (size_t j = i; j < n; j++) d[j] = (unsigned char)c;

        CRT_LEAVE();
        g_crtMemsetHits++;
        return dst;
    } __except(EXCEPTION_EXECUTE_HANDLER) { CRT_LEAVE(); }
fallback:
    g_crtMemsetFallbacks++;
    return orig_memset ? orig_memset(dst, c, n) : dst;
}

// ================================================================
// Installation
// ================================================================
bool InstallCrtMemFastPaths() {
#if TEST_DISABLE_CRT_MEM_FASTPATHS
    Log("[CRT] Fast paths: DISABLED");
    return false;
#else
    HMODULE hCRT = GetModuleHandleA("msvcrt.dll");
    if (!hCRT) { Log("[CRT] msvcrt.dll not found"); return false; }

    int ok = 0;
    auto tryHook = [&](const char* name, void* hook, void** orig) {
        void* p = GetProcAddress(hCRT, name);
        if (p && MH_CreateHook(p, hook, orig) == MH_OK && WO_EnableHook(p) == MH_OK) {
            ok++;
            return true;
        }
        return false;
    };

    tryHook("strlen", (void*)hooked_strlen, (void**)&orig_strlen);
    tryHook("strcmp", (void*)hooked_strcmp, (void**)&orig_strcmp);
    tryHook("memcmp", (void*)hooked_memcmp, (void**)&orig_memcmp);
    tryHook("memcpy", (void*)hooked_memcpy, (void**)&orig_memcpy);
    tryHook("memset", (void*)hooked_memset, (void**)&orig_memset);
    tryHook("strncmp", (void*)hooked_strncmp, (void**)&orig_strncmp);

    // Set the init-readiness gate AFTER all originals are captured.
    // This prevents hooks from firing during the install sequence
    // itself (if a queued enable triggers a CRT call before we finish).
    InterlockedExchange(&g_crtReady, 1);

    if (ok > 0) {
        Log("[CRT] Fast paths: ACTIVE (%d/6 hooked, SSE2 optimized)", ok);
        return true;
    }
    Log("[CRT] Fast paths: FAILED (no hooks installed)");
    return false;
#endif
}