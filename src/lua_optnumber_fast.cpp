#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

// luaL_optnumber at 0x84FB60 — validate optional number argument.
// 13 static callers. Same pattern as luaL_checknumber but with default value.
// Fast path: call lua_tonumber inline, if non-zero return it, else fall through.

typedef double(__cdecl *optnum_fn)(uintptr_t L, int narg, double def);
static optnum_fn orig = nullptr;
static volatile long g_hits = 0, g_misses = 0;

static double __cdecl hook(uintptr_t L, int narg, double def) {
    typedef double(__cdecl *tonumber_fn)(uintptr_t, int);
    double d = ((tonumber_fn)0x0084E030)(L, narg);
    if (d != 0.0) {
        g_hits++;
        return d;
    }
    g_misses++;
    return orig(L, narg, def);
}

bool InstallLuaOptnumberFast() {
    void* t = (void*)0x0084FB60;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    MH_EnableHook(t);
    Log("[OptNum] ACTIVE — luaL_optnumber inline at 0x84FB60");
    CrashDumper::RegisterFeature("OptNum");
    CrashDumper::FeatureSetActive("OptNum", true);
    return true;
}
