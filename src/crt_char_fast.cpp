// CRT memchr + strchr SSE2 replacements — algorithmic, not cache
// Same pattern as our 5 verified CRT SSE2 hooks (strlen/strcmp/memcpy/memset/memcmp)

#include "crt_char_fast.h"
#include "version.h"
#include "MinHook.h"
#include <cstdint>
#include <intrin.h>

extern "C" void Log(const char* fmt, ...);

#if !TEST_DISABLE_CRT_CHAR_SSE2

// ====== memchr ======
typedef void* (__cdecl *memchr_fn)(const void*, int, size_t);
static memchr_fn orig_memchr = nullptr;

static void* __cdecl Hooked_memchr(const void* ptr, int value, size_t num) {
    if (!ptr || num == 0) return nullptr;
    const unsigned char* p = (const unsigned char*)ptr;
    unsigned char c = (unsigned char)value;

    // Align to 16 bytes
    while (((uintptr_t)p & 15) && num > 0) {
        if (*p == c) return (void*)p;
        p++; num--;
    }
    if (num == 0) return nullptr;

    __m128i cmpv = _mm_set1_epi8((char)c);
    while (num >= 16) {
        __m128i v = _mm_load_si128((__m128i*)p);
        int mask = _mm_movemask_epi8(_mm_cmpeq_epi8(v, cmpv));
        if (mask) {
            unsigned long idx;
            _BitScanForward(&idx, mask);
            return (void*)(p + idx);
        }
        p += 16; num -= 16;
    }
    while (num--) { if (*p == c) return (void*)p; p++; }
    return nullptr;
}

// ====== strchr ======
typedef char* (__cdecl *strchr_fn)(const char*, int);
static strchr_fn orig_strchr = nullptr;

static char* __cdecl Hooked_strchr(const char* str, int value) {
    if (!str) return nullptr;
    unsigned char c = (unsigned char)value;
    const char* p = str;

    // Align to 16 bytes
    while (((uintptr_t)p & 15) && *p) {
        if ((unsigned char)*p == c) return (char*)p;
        p++;
    }
    if (!*p) return (c == 0) ? (char*)p : nullptr;

    __m128i cmpv = _mm_set1_epi8((char)c);
    __m128i zero = _mm_setzero_si128();
    for (;;) {
        __m128i v = _mm_load_si128((__m128i*)p);
        __m128i eq = _mm_cmpeq_epi8(v, cmpv);
        __m128i end = _mm_cmpeq_epi8(v, zero);
        int mask = _mm_movemask_epi8(_mm_or_si128(eq, end));
        if (mask) {
            unsigned long idx;
            _BitScanForward(&idx, mask);
            unsigned char found = (unsigned char)p[idx];
            if (found == 0) {
                return (unsigned char)*((unsigned char*)p + idx) == 0 
                    ? (c == 0 ? (char*)(p + idx) : nullptr)
                    : (char*)(p + idx);
            }
            if (found == c) return (char*)(p + idx);
            // Hit null byte first
            return (c == 0) ? (char*)(p + idx) : nullptr;
        }
        p += 16;
    }
}

bool InstallCrtCharSSE2() {
    HMODULE crt = GetModuleHandleA("msvcrt.dll");
    if (!crt) crt = GetModuleHandleA("ucrtbase.dll");

    bool ok = false;
    if (crt) {
        void* ma = GetProcAddress(crt, "memchr");
        void* sa = GetProcAddress(crt, "strchr");
        if (ma && MH_CreateHook(ma, Hooked_memchr, (void**)&orig_memchr) == MH_OK) {
            MH_EnableHook(ma); ok = true;
        }
        if (sa && MH_CreateHook(sa, Hooked_strchr, (void**)&orig_strchr) == MH_OK) {
            MH_EnableHook(sa); ok = true;
        }
    } else {
        void* ma = GetProcAddress(GetModuleHandleA("ntdll.dll"), "memchr");
        void* sa = GetProcAddress(GetModuleHandleA("ntdll.dll"), "strchr");
        if (ma) { MH_CreateHook(ma, Hooked_memchr, (void**)&orig_memchr); MH_EnableHook(ma); ok = true; }
        if (sa) { MH_CreateHook(sa, Hooked_strchr, (void**)&orig_strchr); MH_EnableHook(sa); ok = true; }
    }
    if (ok) Log("[CrtChar] Active: memchr + strchr SSE2 (16-byte SIMD scan)");
    return ok;
}

void ShutdownCrtCharSSE2() {
    if (orig_memchr) MH_DisableHook((void*)orig_memchr);
    if (orig_strchr) MH_DisableHook((void*)orig_strchr);
}

#else
bool InstallCrtCharSSE2() { return false; }
void ShutdownCrtCharSSE2() {}
#endif
