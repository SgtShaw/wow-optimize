#include <windows.h>
#include <cstdint>
#include <cstring>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

#define LUA_TSTRING           4
#define TAINT_CELL 0x00D4139C

// lua_concat at 0x857900 — string concatenation (L, n)
// Strings at top-(16*n) through top-16
typedef int(__cdecl *concat_fn)(uintptr_t L, int n);
static concat_fn orig = nullptr;
static volatile long g_hits = 0, g_misses = 0;

static int __cdecl hook(uintptr_t L, int n) {
    if (L < 0x10000 || L > 0xBFFF0000) { g_misses++; return orig(L, n); }

    uintptr_t top = *(uintptr_t*)(L + 0x0C);
    if (top < 0x10000 || top > 0xBFFF0000) { g_misses++; return orig(L, n); }

    __try {
        // Only optimize the common 2-string case
        if (n != 2) { g_misses++; return orig(L, n); }

        uintptr_t s1_tv = top - 32;
        uintptr_t s2_tv = top - 16;

        if (*(int*)(s1_tv + 8) != LUA_TSTRING || *(int*)(s2_tv + 8) != LUA_TSTRING) {
            g_misses++; return orig(L, n);
        }

        uintptr_t s1 = *(uintptr_t*)s1_tv;
        uintptr_t s2 = *(uintptr_t*)s2_tv;
        if (s1 < 0x10000 || s2 < 0x10000) { g_misses++; return orig(L, n); }

        size_t len1 = *(size_t*)(s1 + 16);
        size_t len2 = *(size_t*)(s2 + 16);
        size_t total = len1 + len2;

        // Guard against overflow — engine raises "string length overflow" beyond 0x1000000
        if (total > 0xFFFFFF) { g_misses++; return orig(L, n); }

        // Allocate buffer via engine's allocator
        typedef void*(__cdecl *alloc_fn)(uintptr_t, uintptr_t, size_t);
        void* buf = ((alloc_fn)0x0085D170)(L, *(uintptr_t*)(*(uintptr_t*)(L + 0x14) + 52), total);
        if (!buf) { g_misses++; return orig(L, n); }

        const char* data1 = (const char*)(s1 + 20);
        const char* data2 = (const char*)(s2 + 20);
        memcpy(buf, data1, len1);
        memcpy((char*)buf + len1, data2, len2);

        // Create TString via luaS_newlstr
        typedef uintptr_t(__cdecl *newlstr_fn)(uintptr_t, const void*, size_t);
        uintptr_t new_ts = ((newlstr_fn)0x00856C80)(L, buf, total);

        // Pop both strings, push result
        uint32_t taint = *(uint32_t*)TAINT_CELL;
        *(uintptr_t*)(s1_tv + 0) = new_ts;
        *(uint32_t*)(s1_tv + 8) = LUA_TSTRING;
        *(uint32_t*)(s1_tv + 12) = taint;
        *(uintptr_t*)(L + 0x0C) = s1_tv + 16;

        g_hits++;
        return (int)L;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    g_misses++;
    return orig(L, n);
}

bool InstallLuaConcatFast() {
    void* t = (void*)0x00857900;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    WO_EnableHook(t);
    Log("[ConcatFast] ACTIVE — lua_concat inline at 0x857900");
    CrashDumper::RegisterFeature("ConcatFast");
    CrashDumper::FeatureSetActive("ConcatFast", true);
    return true;
}
