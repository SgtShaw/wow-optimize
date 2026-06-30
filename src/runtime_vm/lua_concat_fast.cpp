// ============================================================================
// Module: lua_concat_fast.cpp
// Description: Accelerates Lua runtime calls in `lua_concat_fast.cpp`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================

#include <windows.h>
#include <cstdint>
#include <cstring>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

#define LUA_TSTRING           4
#define TAINT_CELL 0x00D4139C

// luaV_concat at 0x857900 — __cdecl(L, total, last). Concatenates `total` values
// ending at stack slot base+last. The earlier 2-arg (L, n) prototype dropped the
// `last` argument, so deferring to the engine passed a garbage `last` and faulted
// at luaV_concat+0x24 (ebx = base + (last+1)*16). Operand positions are derived
// from base+last exactly as the engine does (ebx-8 = right, ebx-24 = left).
typedef int(__cdecl *concat_fn)(uintptr_t L, int total, int last);
static concat_fn orig = nullptr;
static volatile long g_hits = 0, g_misses = 0;

static int __cdecl hook(uintptr_t L, int total, int last) {
    if (L < 0x10000 || L > 0xBFFF0000) { g_misses++; return orig(L, total, last); }

    __try {
        if (total < 2 || total > 4 || last < total - 1) { g_misses++; return orig(L, total, last); }

        uintptr_t base = *(uintptr_t*)(L + 0x10);
        if (base < 0x10000) { g_misses++; return orig(L, total, last); }

        char buf[4096];
        size_t tlen = 0;

        for (int i = 0; i < total; ++i) {
            uintptr_t tv = base + (uintptr_t)(last - total + 1 + i) * 16;
            if (*(int*)(tv + 8) != LUA_TSTRING) { g_misses++; return orig(L, total, last); }
            uintptr_t ts = *(uintptr_t*)tv;
            if (ts < 0x10000) { g_misses++; return orig(L, total, last); }
            size_t slen = *(size_t*)(ts + 16);
            if (slen == 0) continue;
            tlen += slen;
            if (tlen > 4000) { g_misses++; return orig(L, total, last); }
            memcpy(buf + tlen - slen, (const char*)(ts + 20), slen);
        }

        if (tlen == 0) { g_misses++; return orig(L, total, last); }

        typedef uintptr_t(__cdecl *newlstr_fn)(uintptr_t, const void*, size_t);
        uintptr_t new_ts = ((newlstr_fn)0x00856C80)(L, buf, tlen);
        if (new_ts < 0x10000) { g_misses++; return orig(L, total, last); }

        // Re-read base in case GC reallocated stack!
        uintptr_t new_base = *(uintptr_t*)(L + 0x10);
        if (new_base < 0x10000) { g_misses++; return orig(L, total, last); }
        uintptr_t new_s1_tv = new_base + (uintptr_t)(last - total + 1) * 16;

        uint32_t taint = *(uint32_t*)TAINT_CELL;
        *(uintptr_t*)(new_s1_tv + 0) = new_ts;
        *(uint32_t*)(new_s1_tv + 4) = 0;
        *(uint32_t*)(new_s1_tv + 8) = LUA_TSTRING;
        *(uint32_t*)(new_s1_tv + 12) = taint;

        g_hits++;
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    g_misses++;
    return orig(L, total, last);
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
