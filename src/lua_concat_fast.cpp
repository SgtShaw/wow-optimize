#include <windows.h>
#include <cstdint>
#include <cstring>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

#define LUA_TSTRING           4
#define TAINT_CELL 0x00D4139C

typedef int(__cdecl *concat_fn)(uintptr_t L, int total, int last);
static concat_fn orig = nullptr;
static volatile long g_hits = 0, g_misses = 0;

static int __cdecl hook(uintptr_t L, int total, int last) {
    if (L < 0x10000 || L > 0xBFFF0000) { g_misses++; return orig(L, total, last); }

    __try {
        if (total != 2 || last < 1) { g_misses++; return orig(L, total, last); }

        uintptr_t base = *(uintptr_t*)(L + 0x10);
        if (base < 0x10000) { g_misses++; return orig(L, total, last); }

        uintptr_t right_tv = base + (uintptr_t)last * 16;
        uintptr_t left_tv  = base + (uintptr_t)(last - 1) * 16;

        if (*(int*)(left_tv + 8) != LUA_TSTRING || *(int*)(right_tv + 8) != LUA_TSTRING) {
            g_misses++; return orig(L, total, last);
        }

        uintptr_t left_ts  = *(uintptr_t*)left_tv;
        uintptr_t right_ts = *(uintptr_t*)right_tv;
        if (left_ts < 0x10000 || right_ts < 0x10000) { g_misses++; return orig(L, total, last); }

        size_t left_len  = *(size_t*)(left_ts  + 16);
        size_t right_len = *(size_t*)(right_ts + 16);
        size_t tlen = left_len + right_len;

        if (tlen > 4000) { g_misses++; return orig(L, total, last); }

        char buf[4096];
        memcpy(buf, (const char*)(left_ts + 20), left_len);
        if (right_len)
            memcpy(buf + left_len, (const char*)(right_ts + 20), right_len);

        typedef uintptr_t(__cdecl *newlstr_fn)(uintptr_t, const void*, size_t);
        uintptr_t new_ts = ((newlstr_fn)0x00856C80)(L, buf, tlen);
        if (new_ts < 0x10000) { g_misses++; return orig(L, total, last); }

        uint32_t taint = *(uint32_t*)TAINT_CELL;
        *(uintptr_t*)(left_tv + 0)  = new_ts;
        *(uint32_t*)(left_tv + 8)  = LUA_TSTRING;
        *(uint32_t*)(left_tv + 12) = taint;
        *(uintptr_t*)(L + 0x0C) = right_tv;

        g_hits++;
        return (int)L;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    g_misses++;
    return orig(L, total, last);
}

bool InstallLuaConcatFast() {
    void* t = (void*)0x00857900;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    WO_EnableHook(t);
    Log("[ConcatFast] ACTIVE — lua_concat 2-string fast path at 0x857900");
    CrashDumper::RegisterFeature("ConcatFast");
    CrashDumper::FeatureSetActive("ConcatFast", true);
    return true;
}
