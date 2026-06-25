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
        // Only optimize the common two-string concatenation.
        if (total != 2 || last < 1) { g_misses++; return orig(L, total, last); }

        uintptr_t base = *(uintptr_t*)(L + 0x10);
        if (base < 0x10000) { g_misses++; return orig(L, total, last); }

        uintptr_t s1_tv = base + (uintptr_t)(last - 1) * 16;  // left operand
        uintptr_t s2_tv = base + (uintptr_t)last * 16;        // right operand

        if (*(int*)(s1_tv + 8) != LUA_TSTRING || *(int*)(s2_tv + 8) != LUA_TSTRING) {
            g_misses++; return orig(L, total, last);
        }

        uintptr_t s1 = *(uintptr_t*)s1_tv;
        uintptr_t s2 = *(uintptr_t*)s2_tv;
        if (s1 < 0x10000 || s2 < 0x10000) { g_misses++; return orig(L, total, last); }

        size_t len1 = *(size_t*)(s1 + 16);
        size_t len2 = *(size_t*)(s2 + 16);
        size_t tlen = len1 + len2;

        // Only short concatenations take the fast path so a fixed stack buffer is
        // always large enough. Anything bigger defers to the engine, which manages
        // its own growable string buffer.
        if (tlen == 0 || tlen > 4000) { g_misses++; return orig(L, total, last); }

        char buf[4000];
        memcpy(buf, (const char*)(s1 + 20), len1);
        memcpy(buf + len1, (const char*)(s2 + 20), len2);

        // Create TString via luaS_newlstr (interns + copies out of buf)
        typedef uintptr_t(__cdecl *newlstr_fn)(uintptr_t, const void*, size_t);
        uintptr_t new_ts = ((newlstr_fn)0x00856C80)(L, buf, tlen);
        if (new_ts < 0x10000) { g_misses++; return orig(L, total, last); }

        // Write the result into the left operand's slot, exactly like the engine
        // (value/tt/taint at s1_tv). luaV_concat itself does NOT touch L->top — the
        // caller (the VM's OP_CONCAT or lua_concat) adjusts the stack afterwards, so
        // the hook must leave L->top alone to avoid a double pop.
        uint32_t taint = *(uint32_t*)TAINT_CELL;
        *(uintptr_t*)(s1_tv + 0) = new_ts;
        *(uint32_t*)(s1_tv + 8) = LUA_TSTRING;
        *(uint32_t*)(s1_tv + 12) = taint;

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
    Log("[ConcatFast] ACTIVE — lua_concat inline at 0x857900");
    CrashDumper::RegisterFeature("ConcatFast");
    CrashDumper::FeatureSetActive("ConcatFast", true);
    return true;
}
