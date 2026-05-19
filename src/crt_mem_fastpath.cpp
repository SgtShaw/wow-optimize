// ================================================================
// CRT Memory/String Fast Paths — SSE2 optimized msvcrt hooks
//
// ================================================================

#include <windows.h>
#include <intrin.h>
#include <emmintrin.h>
#include "MinHook.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

// Stats (defined in dllmain.cpp)
extern long g_crtStrlenHits, g_crtStrlenFallbacks;
extern long g_crtStrcmpHits, g_crtStrcmpFallbacks;
extern long g_crtMemcmpHits, g_crtMemcmpFallbacks;
extern long g_crtMemcpyHits, g_crtMemcpyFallbacks;
extern long g_crtMemsetHits, g_crtMemsetFallbacks;

// Thread-local recursion guard — prevents circular re-entry between
// mimalloc and CRT hooks. mimalloc may call memset/memcpy while holding
// internal locks; if our hook then triggers another allocation, deadlock.
static __declspec(thread) volatile LONG g_crtInHook = 0;

#define CRT_ENTER() do { \
    if (InterlockedExchange(&g_crtInHook, 1) != 0) goto fallback; \
} while(0)
#define CRT_LEAVE() InterlockedExchange(&g_crtInHook, 0)

// Originals
typedef size_t (__cdecl* strlen_fn)(const char*);
typedef int   (__cdecl* strcmp_fn)(const char*, const char*);
typedef int   (__cdecl* memcmp_fn)(const void*, const void*, size_t);
typedef void* (__cdecl* memcpy_fn)(void*, const void*, size_t);
typedef void* (__cdecl* memset_fn)(void*, int, size_t);

static strlen_fn  orig_strlen  = nullptr;
static strcmp_fn  orig_strcmp  = nullptr;
static memcmp_fn  orig_memcmp  = nullptr;
static memcpy_fn  orig_memcpy  = nullptr;
static memset_fn  orig_memset  = nullptr;

// ================================================================
// strlen — SSE2 null-terminator scan
// ================================================================
static size_t __cdecl hooked_strlen(const char* s) {
    CRT_ENTER();
    if (!s) { CRT_LEAVE(); goto fallback; }
    __try {
        const __m128i zero = _mm_setzero_si128();
        size_t len = 0;
        while (true) {
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
    return orig_strlen(s);
}

// ================================================================
// strcmp — SSE2 byte-wise compare (ASCII-safe)
// ================================================================
static int __cdecl hooked_strcmp(const char* s1, const char* s2) {
    CRT_ENTER();
    if (!s1 || !s2) { CRT_LEAVE(); goto fallback; }
    __try {
        const __m128i zero = _mm_setzero_si128();
        size_t i = 0;
        while (true) {
            __m128i a = _mm_loadu_si128((const __m128i*)(s1 + i));
            __m128i b = _mm_loadu_si128((const __m128i*)(s2 + i));
            int diff = _mm_movemask_epi8(_mm_cmpeq_epi8(a, b));
            if (diff != 0xFFFF) {
                unsigned long idx;
                _BitScanForward(&idx, (unsigned long)(~diff & 0xFFFF));
                int result = (unsigned char)s1[i + idx] - (unsigned char)s2[i + idx];
                CRT_LEAVE();
                g_crtStrcmpHits++;
                return result;
            }
            int nullA = _mm_movemask_epi8(_mm_cmpeq_epi8(a, zero));
            int nullB = _mm_movemask_epi8(_mm_cmpeq_epi8(b, zero));
            if (nullA || nullB) {
                CRT_LEAVE();
                g_crtStrcmpHits++;
                return 0;
            }
            i += 16;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { CRT_LEAVE(); }
fallback:
    g_crtStrcmpFallbacks++;
    return orig_strcmp(s1, s2);
}

// ================================================================
// memcmp — SSE2 vectorized comparison
// ================================================================
static int __cdecl hooked_memcmp(const void* s1, const void* s2, size_t n) {
    CRT_ENTER();
    if (!s1 || !s2 || n == 0) { CRT_LEAVE(); goto fallback; }
    __try {
        const __m128i* p1 = (const __m128i*)s1;
        const __m128i* p2 = (const __m128i*)s2;
        size_t i = 0;
        for (; i + 16 <= n; i += 16, p1++, p2++) {
            __m128i a = _mm_loadu_si128(p1);
            __m128i b = _mm_loadu_si128(p2);
            int eq = _mm_movemask_epi8(_mm_cmpeq_epi8(a, b));
            if (eq != 0xFFFF) {
                unsigned long idx;
                _BitScanForward(&idx, (unsigned long)(~eq & 0xFFFF));
                int result = ((const unsigned char*)s1)[i + idx] - ((const unsigned char*)s2)[i + idx];
                CRT_LEAVE();
                g_crtMemcmpHits++;
                return result;
            }
        }
        for (; i < n; i++) {
            int diff = ((const unsigned char*)s1)[i] - ((const unsigned char*)s2)[i];
            if (diff) { CRT_LEAVE(); g_crtMemcmpHits++; return diff; }
        }
        CRT_LEAVE();
        g_crtMemcmpHits++;
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { CRT_LEAVE(); }
fallback:
    g_crtMemcmpFallbacks++;
    return orig_memcmp(s1, s2, n);
}

// ================================================================
// memcpy — SSE2 unaligned copy (safe for overlapping regions)
// ================================================================
static void* __cdecl hooked_memcpy(void* dst, const void* src, size_t n) {
    CRT_ENTER();
    if (!dst || !src || n == 0) { CRT_LEAVE(); goto fallback; }
    __try {
        if (src < dst && (const char*)src + n > (char*)dst) { CRT_LEAVE(); goto fallback; }

        __m128i* d = (__m128i*)dst;
        const __m128i* s = (const __m128i*)src;
        size_t i = 0;
        for (; i + 16 <= n; i += 16, d++, s++) {
            _mm_storeu_si128(d, _mm_loadu_si128(s));
        }
        unsigned char* d8 = (unsigned char*)d;
        const unsigned char* s8 = (const unsigned char*)s;
        for (size_t j = i; j < n; j++) *d8++ = *s8++;

        CRT_LEAVE();
        g_crtMemcpyHits++;
        return dst;
    } __except(EXCEPTION_EXECUTE_HANDLER) { CRT_LEAVE(); }
fallback:
    g_crtMemcpyFallbacks++;
    return orig_memcpy(dst, src, n);
}

// ================================================================
// memset — SSE2 byte broadcast
// ================================================================
static void* __cdecl hooked_memset(void* dst, int c, size_t n) {
    CRT_ENTER();
    if (!dst || n == 0) { CRT_LEAVE(); goto fallback; }
    __try {
        __m128i val = _mm_set1_epi8((char)c);
        __m128i* d = (__m128i*)dst;
        size_t i = 0;
        for (; i + 16 <= n; i += 16, d++) {
            _mm_storeu_si128(d, val);
        }
        unsigned char* d8 = (unsigned char*)d;
        for (size_t j = i; j < n; j++) *d8++ = (unsigned char)c;

        CRT_LEAVE();
        g_crtMemsetHits++;
        return dst;
    } __except(EXCEPTION_EXECUTE_HANDLER) { CRT_LEAVE(); }
fallback:
    g_crtMemsetFallbacks++;
    return orig_memset(dst, c, n);
}

// ================================================================
// Installation
// ================================================================
bool InstallCrtMemFastPaths() {
#if TEST_DISABLE_CRT_MEM_FASTPATHS
    Log("[CRT] Fast paths: DISABLED (bisection toggle)");
    return false;
#else
    HMODULE hCRT = GetModuleHandleA("msvcrt.dll");
    if (!hCRT) { Log("[CRT] msvcrt.dll not found"); return false; }

    int ok = 0;
    auto tryHook = [&](const char* name, void* hook, void** orig) {
        void* p = GetProcAddress(hCRT, name);
        if (p && MH_CreateHook(p, hook, orig) == MH_OK && MH_EnableHook(p) == MH_OK) {
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

    if (ok > 0) {
        Log("[CRT] Fast paths: ACTIVE (%d/5 hooked, SSE2 optimized)", ok);
        return true;
    }
    Log("[CRT] Fast paths: FAILED (no hooks installed)");
    return false;
#endif
}