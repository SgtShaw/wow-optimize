// CRT memchr + strchr + strcpy SSE2 replacements - algorithmic, not cache
// Same pattern as verified CRT SSE2 hooks (strlen/strcmp/memcpy/memset/memcmp)

#include "crt_char_fast.h"
#include "version.h"
#include "MinHook.h"
#include <cstdint>
#include <intrin.h>

extern "C" void Log(const char* fmt, ...);

#if !TEST_DISABLE_CRT_CHAR_SSE2

// Stats defined in dllmain.cpp
extern volatile LONG64 g_memchrHits, g_memchrFallbacks;
extern volatile LONG64 g_strchrHits, g_strchrFallbacks;
extern volatile LONG64 g_strcpyHits, g_strcpyFallbacks;

// Thread-local recursion guard
static __declspec(thread) volatile LONG g_charInHook = 0;
#define CHAR_ENTER() do { if (InterlockedExchange(&g_charInHook, 1) != 0) goto fallback; } while(0)
#define CHAR_LEAVE() InterlockedExchange(&g_charInHook, 0)

// ====== memchr ======
typedef void* (__cdecl *memchr_fn)(const void*, int, size_t);
static memchr_fn orig_memchr = nullptr;

static void* __cdecl Hooked_memchr(const void* ptr, int value, size_t num) {
    CHAR_ENTER();
    if (!ptr || num == 0) { CHAR_LEAVE(); goto fallback; }
    if (((uintptr_t)ptr & 0xFFF) > 0xFF0) { CHAR_LEAVE(); goto fallback; }
    __try {
        const unsigned char* p = (const unsigned char*)ptr;
        unsigned char c = (unsigned char)value;
        while (((uintptr_t)p & 15) && num > 0) {
            if (*p == c) { CHAR_LEAVE(); InterlockedIncrement64(&g_memchrHits); return (void*)p; }
            p++; num--;
        }
        if (num == 0) { CHAR_LEAVE(); InterlockedIncrement64(&g_memchrHits); return nullptr; }
        __m128i cmpv = _mm_set1_epi8((char)c);
        while (num >= 16) {
            __m128i v = _mm_loadu_si128((__m128i*)p);
            int mask = _mm_movemask_epi8(_mm_cmpeq_epi8(v, cmpv));
            if (mask) {
                unsigned long idx; _BitScanForward(&idx, mask);
                CHAR_LEAVE(); InterlockedIncrement64(&g_memchrHits); return (void*)(p + idx);
            }
            p += 16; num -= 16;
        }
        while (num--) { if (*p == c) { CHAR_LEAVE(); InterlockedIncrement64(&g_memchrHits); return (void*)p; } p++; }
        CHAR_LEAVE(); InterlockedIncrement64(&g_memchrHits); return nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { CHAR_LEAVE(); }
fallback:
    InterlockedIncrement64(&g_memchrFallbacks);
    return orig_memchr(ptr, value, num);
}

// ====== strchr ======
typedef char* (__cdecl *strchr_fn)(const char*, int);
static strchr_fn orig_strchr = nullptr;

static char* __cdecl Hooked_strchr(const char* str, int value) {
    CHAR_ENTER();
    if (!str) { CHAR_LEAVE(); goto fallback; }
    if (((uintptr_t)str & 0xFFF) > 0xFF0) { CHAR_LEAVE(); goto fallback; }
    __try {
        unsigned char c = (unsigned char)value;
        const char* p = str;
        while (((uintptr_t)p & 15) && *p) {
            if ((unsigned char)*p == c) { CHAR_LEAVE(); InterlockedIncrement64(&g_strchrHits); return (char*)p; }
            p++;
        }
        if (!*p) { CHAR_LEAVE(); InterlockedIncrement64(&g_strchrHits); return (c == 0) ? (char*)p : nullptr; }
        __m128i cmpv = _mm_set1_epi8((char)c);
        __m128i zero = _mm_setzero_si128();
        for (;;) {
            __m128i v = _mm_loadu_si128((__m128i*)p);
            __m128i eq = _mm_cmpeq_epi8(v, cmpv);
            __m128i end = _mm_cmpeq_epi8(v, zero);
            int mask = _mm_movemask_epi8(_mm_or_si128(eq, end));
            if (mask) {
                unsigned long idx; _BitScanForward(&idx, mask);
                unsigned char found = (unsigned char)p[idx];
                CHAR_LEAVE(); InterlockedIncrement64(&g_strchrHits);
                if (found == 0) return (c == 0 ? (char*)(p + idx) : nullptr);
                if (found == c) return (char*)(p + idx);
                return (c == 0) ? (char*)(p + idx) : nullptr;
            }
            p += 16;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { CHAR_LEAVE(); }
fallback:
    InterlockedIncrement64(&g_strchrFallbacks);
    return orig_strchr(str, value);
}

// ====== strcpy ======
typedef char* (__cdecl *strcpy_fn)(char*, const char*);
static strcpy_fn orig_strcpy = nullptr;

static char* __cdecl Hooked_strcpy(char* dst, const char* src) {
    CHAR_ENTER();
    if (!dst || !src) { CHAR_LEAVE(); goto fallback; }
    if (((uintptr_t)dst & 0xFFF) > 0xFF0 || ((uintptr_t)src & 0xFFF) > 0xFF0)
        { CHAR_LEAVE(); goto fallback; }
    __try {
        char* ret = dst;
        const __m128i zero = _mm_setzero_si128();
        for (;;) {
            __m128i v = _mm_loadu_si128((const __m128i*)src);
            _mm_storeu_si128((__m128i*)dst, v);
            int mask = _mm_movemask_epi8(_mm_cmpeq_epi8(v, zero));
            if (mask) { CHAR_LEAVE(); InterlockedIncrement64(&g_strcpyHits); return ret; }
            src += 16; dst += 16;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { CHAR_LEAVE(); }
fallback:
    InterlockedIncrement64(&g_strcpyFallbacks);
    return orig_strcpy(dst, src);
}

// ====== strcat ======
typedef char* (__cdecl *strcat_fn)(char*, const char*);
static strcat_fn orig_strcat = nullptr;

static char* __cdecl Hooked_strcat(char* dst, const char* src) {
    CHAR_ENTER();
    if (!dst || !src) { CHAR_LEAVE(); goto fallback; }
    if (((uintptr_t)dst & 0xFFF) > 0xFF0 || ((uintptr_t)src & 0xFFF) > 0xFF0) { CHAR_LEAVE(); goto fallback; }
    __try {
        const __m128i zero = _mm_setzero_si128();
        size_t dlen = 0;
        while (true) {
            __m128i v = _mm_loadu_si128((const __m128i*)(dst + dlen));
            int mask = _mm_movemask_epi8(_mm_cmpeq_epi8(v, zero));
            if (mask) { unsigned long idx; _BitScanForward(&idx, mask); dlen += idx; break; }
            dlen += 16;
        }
        char* d = dst + dlen;
        while (true) {
            __m128i v = _mm_loadu_si128((const __m128i*)src);
            _mm_storeu_si128((__m128i*)d, v);
            int mask = _mm_movemask_epi8(_mm_cmpeq_epi8(v, zero));
            if (mask) { CHAR_LEAVE(); InterlockedIncrement64(&g_strcpyHits); return dst; }
            src += 16; d += 16;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { CHAR_LEAVE(); }
fallback: InterlockedIncrement64(&g_strcpyFallbacks); return orig_strcat(dst, src);
}

bool InstallCrtCharSSE2() {
    HMODULE crt = GetModuleHandleA("msvcrt.dll");
    if (!crt) crt = GetModuleHandleA("ucrtbase.dll");
    if (!crt) { Log("[CrtChar] msvcrt/ucrtbase not found"); return false; }

    int ok = 0;
    auto tryHook = [&](const char* name, void* hook, void** orig) {
        void* p = GetProcAddress(crt, name);
        if (p && MH_CreateHook(p, hook, orig) == MH_OK && MH_EnableHook(p) == MH_OK) ok++;
    };

    tryHook("memchr",  (void*)Hooked_memchr,  (void**)&orig_memchr);
    tryHook("strchr",  (void*)Hooked_strchr,  (void**)&orig_strchr);
    tryHook("strcpy",  (void*)Hooked_strcpy,  (void**)&orig_strcpy);
    // tryHook("strcat",  (void*)Hooked_strcat,  (void**)&orig_strcat); // DISABLED - crash isolation

    if (ok > 0) {
        Log("[CrtChar] Active: memchr+strchr+strcpy SSE2 (%d/3 hooked, page-boundary guarded)", ok);
        return true;
    }
    Log("[CrtChar] No hooks installed");
    return false;
}

void ShutdownCrtCharSSE2() {
    if (orig_memchr)  MH_DisableHook((void*)orig_memchr);
    if (orig_strchr)  MH_DisableHook((void*)orig_strchr);
    if (orig_strcpy)  MH_DisableHook((void*)orig_strcpy);
    if (orig_strcat)  MH_DisableHook((void*)orig_strcat);
}

#else
bool InstallCrtCharSSE2() { return false; }
void ShutdownCrtCharSSE2() {}
#endif