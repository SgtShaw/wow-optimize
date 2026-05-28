#include <windows.h>
#include <MinHook.h>
#include <cstdint>
#include <cstring>
#include <mimalloc.h>
#include <emmintrin.h>  // SSE2
#include "version.h"

extern "C" void Log(const char* fmt, ...);

// ================================================================
// Statistics
// ================================================================
static volatile long g_free_calls = 0;
static volatile long g_free_fast = 0;
static volatile long g_strnicmp_calls = 0;
static volatile long g_strnicmp_sse2 = 0;
static volatile long g_jenkins_calls = 0;
static volatile long g_jenkins_fast = 0;

// ================================================================
// Original function pointers
// ================================================================
typedef int  (__stdcall* FreeWrapper_t)(void*, int, int, int);
typedef int  (__cdecl* Strnicmp_t)(const char*, const char*, size_t);
typedef uint32_t (__cdecl* JenkinsHash_t)(const uint8_t*, uint32_t, uint32_t);

static FreeWrapper_t  pOrigFree = nullptr;
static Strnicmp_t     pOrigStrnicmp = nullptr;
static JenkinsHash_t  pOrigJenkins = nullptr;

// ================================================================
// sub_76E5A0: Custom free wrapper (2901 xrefs)
// Original: _msize(Block); free(Block); return 1;
// Optimization: skip _msize entirely, call mi_free directly
// _msize is expensive (heap walk, lock contention) and result is discarded
// ================================================================
static int __stdcall HookFreeWrapper(void* block, int a2, int a3, int a4) {
#if !TEST_DISABLE_STRING_OPS_FAST
    InterlockedIncrement(&g_free_calls);

    if (block) {
        InterlockedIncrement(&g_free_fast);
        mi_free(block);
    }
    return 1;
#else
    return pOrigFree(block, a2, a3, a4);
#endif
}

// ================================================================
// sub_76E780: strnicmp wrapper (1013 xrefs)
// Original: return _strnicmp(s1, s2, n);
// Optimization: SSE2 ASCII fast path for aligned 16-byte chunks
// ================================================================

// SSE2 tolower for ASCII (A-Z -> a-z, leaves others unchanged)
__forceinline __m128i SSE2_ToLowerASCII(__m128i v) {
    __m128i a_bound = _mm_set1_epi8('A' - 1);
    __m128i z_bound = _mm_set1_epi8('Z' + 1);
    __m128i diff    = _mm_set1_epi8(0x20);

    __m128i ge_a = _mm_cmpgt_epi8(v, a_bound);
    __m128i lt_z = _mm_cmplt_epi8(v, z_bound);
    __m128i is_upper = _mm_and_si128(ge_a, lt_z);

    return _mm_add_epi8(v, _mm_and_si128(is_upper, diff));
}

static int __cdecl HookStrnicmp(const char* s1, const char* s2, size_t max_count) {
#if !TEST_DISABLE_STRING_OPS_FAST
    InterlockedIncrement(&g_strnicmp_calls);

    if (!s1 || !s2) return s1 ? 1 : (s2 ? -1 : 0);
    if (max_count == 0) return 0;

    // SSE2 fast path: both aligned, length >= 16
    bool aligned = (((uintptr_t)s1 & 0xF) == 0) && (((uintptr_t)s2 & 0xF) == 0);

    if (aligned && max_count >= 16) {
        size_t i = 0;
        while (i + 16 <= max_count) {
            __m128i v1 = _mm_load_si128((const __m128i*)(s1 + i));
            __m128i v2 = _mm_load_si128((const __m128i*)(s2 + i));

            __m128i lc1 = SSE2_ToLowerASCII(v1);
            __m128i lc2 = SSE2_ToLowerASCII(v2);

            __m128i cmp = _mm_cmpeq_epi8(lc1, lc2);
            int match_mask = _mm_movemask_epi8(cmp);

            // Check for null terminator in either string
            __m128i zero = _mm_setzero_si128();
            int null_mask = _mm_movemask_epi8(_mm_or_si128(
                _mm_cmpeq_epi8(v1, zero),
                _mm_cmpeq_epi8(v2, zero)));

            if (null_mask != 0) {
                // Null found within this chunk - scalar finish to find exact position
                break;
            }

            if (match_mask != 0xFFFF) {
                // Mismatch - fall through to scalar for exact result
                break;
            }

            i += 16;
        }

        // Scalar finish from position i
        for (; i < max_count; i++) {
            unsigned char c1 = (unsigned char)s1[i];
            unsigned char c2 = (unsigned char)s2[i];
            if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
            if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
            if (c1 != c2) return (int)c1 - (int)c2;
            if (c1 == 0) return 0;
        }

        InterlockedIncrement(&g_strnicmp_sse2);
        return 0;
    }

    // Scalar path (unaligned or short strings)
    for (size_t i = 0; i < max_count; i++) {
        unsigned char c1 = (unsigned char)s1[i];
        unsigned char c2 = (unsigned char)s2[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return (int)c1 - (int)c2;
        if (c1 == 0) return 0;
    }

    InterlockedIncrement(&g_strnicmp_sse2);
    return 0;
#else
    return pOrigStrnicmp(s1, s2, max_count);
#endif
}

// ================================================================
// sub_76F420: Bob Jenkins lookup3 hash (hashlittle2 style)
// Pure function, no side effects - safe to inline and optimize
// The original has a 12-byte block main loop with mix() macro
// We inline with explicit register allocation
// ================================================================
static uint32_t __cdecl HookJenkinsHash(const uint8_t* key, uint32_t length, uint32_t initval) {
#if !TEST_DISABLE_STRING_OPS_FAST
    InterlockedIncrement(&g_jenkins_calls);

    if (!key || length == 0) return initval;

    uint32_t a, b, c;
    a = b = c = 0xdeadbeef + length + initval;

    uint32_t len = length;

    // Main loop: process 12-byte blocks
    while (len > 12) {
        a += key[0]  | ((uint32_t)key[1]  << 8)  | ((uint32_t)key[2]  << 16) | ((uint32_t)key[3]  << 24);
        b += key[4]  | ((uint32_t)key[5]  << 8)  | ((uint32_t)key[6]  << 16) | ((uint32_t)key[7]  << 24);
        c += key[8]  | ((uint32_t)key[9]  << 8)  | ((uint32_t)key[10] << 16) | ((uint32_t)key[11] << 24);

        // mix() macro inlined
        a -= c; a ^= (c << 4)  | (c >> 28); c += b;
        b -= a; b ^= (a << 6)  | (a >> 26); a += c;
        c -= b; c ^= (b << 8)  | (b >> 24); b += a;
        a -= c; a ^= (c << 16) | (c >> 16); c += b;
        b -= a; b ^= (a << 19) | (a >> 13); a += c;
        c -= b; c ^= (b << 4)  | (b >> 28); b += a;

        key += 12;
        len -= 12;
    }

    // Handle remaining 0-12 bytes
    switch (len) {
        case 12: c += ((uint32_t)key[11] << 24); /* fall through */
        case 11: c += ((uint32_t)key[10] << 16); /* fall through */
        case 10: c += ((uint32_t)key[9]  << 8);  /* fall through */
        case 9:  c += key[8];                     /* fall through */
        case 8:  b += ((uint32_t)key[7]  << 24); /* fall through */
        case 7:  b += ((uint32_t)key[6]  << 16); /* fall through */
        case 6:  b += ((uint32_t)key[5]  << 8);  /* fall through */
        case 5:  b += key[4];                     /* fall through */
        case 4:  a += ((uint32_t)key[3]  << 24); /* fall through */
        case 3:  a += ((uint32_t)key[2]  << 16); /* fall through */
        case 2:  a += ((uint32_t)key[1]  << 8);  /* fall through */
        case 1:  a += key[0]; break;
        case 0:  return c;
    }

    // final() mix
    c ^= b; c -= (b << 14) | (b >> 18);
    a ^= c; a -= (c << 11) | (c >> 21);
    b ^= a; b -= (a << 25) | (a >> 7);
    c ^= b; c -= (b << 16) | (b >> 16);
    a ^= c; a -= (c << 4)  | (c >> 28);
    b ^= a; b -= (a << 14) | (a >> 18);
    c ^= b; c -= (b << 24) | (b >> 8);

    InterlockedIncrement(&g_jenkins_fast);
    return c;
#else
    return pOrigJenkins(key, length, initval);
#endif
}

// ================================================================
// Installation
// ================================================================
bool InitStringOpsFast() {
#if TEST_DISABLE_STRING_OPS_FAST
    Log("[StringOps] DISABLED via feature flag");
    return false;
#else
    struct HookDef {
        void*       addr;
        void*       hook;
        void**      orig;
        const char* name;
        uint32_t    xrefs;
    };

    // NOTE: sub_76E5A0 (free wrapper) REMOVED - causes heap corruption with mixed allocators
    // WoW uses CRT malloc, custom allocators, and mimalloc - calling mi_free on CRT memory corrupts heap
    HookDef hooks[] = {
        { (void*)0x0076E780, (void*)HookStrnicmp,     (void**)&pOrigStrnicmp, "strnicmp",     1013 },
        { (void*)0x0076F420, (void*)HookJenkinsHash,  (void**)&pOrigJenkins,  "JenkinsHash",    0  },
    };

    int installed = 0;
    for (auto& h : hooks) {
        if (WineSafe_CreateHook(h.addr, h.hook, h.orig) == MH_OK) {
            if (MH_EnableHook(h.addr) == MH_OK) {
                installed++;
                Log("[StringOps] Hooked %s at 0x%08X (%d xrefs)", h.name, (DWORD)(uintptr_t)h.addr, h.xrefs);
            }
        }
    }

    Log("[StringOps] Installed %d/%d hooks (total %d+ xrefs)",
        installed, (int)(sizeof(hooks)/sizeof(hooks[0])), 1013);
    return installed == (int)(sizeof(hooks)/sizeof(hooks[0]));
#endif
}

// ================================================================
// Statistics dump
// ================================================================
void DumpStringOpsStats() {
#if !TEST_DISABLE_STRING_OPS_FAST
    Log("[StringOps] === String & Memory Ops Statistics ===");
    if (g_free_calls > 0)
        Log("[StringOps] FreeWrapper: %ld/%ld (%.1f%% mi_free bypass)",
            g_free_fast, g_free_calls,
            100.0 * g_free_fast / g_free_calls);
    if (g_strnicmp_calls > 0)
        Log("[StringOps] strnicmp: %ld calls, %ld SSE2 (%.1f%%)",
            g_strnicmp_calls, g_strnicmp_sse2,
            100.0 * g_strnicmp_sse2 / g_strnicmp_calls);
    if (g_jenkins_calls > 0)
        Log("[StringOps] Jenkins hash: %ld calls, %ld inlined (%.1f%%)",
            g_jenkins_calls, g_jenkins_fast,
            100.0 * g_jenkins_fast / g_jenkins_calls);
#endif
}

// ================================================================
// Cleanup
// ================================================================
void ShutdownStringOpsFast() {
#if !TEST_DISABLE_STRING_OPS_FAST
    MH_DisableHook((void*)0x0076E780);
    MH_DisableHook((void*)0x0076F420);
    DumpStringOpsStats();
#endif
}
