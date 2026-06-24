#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

// luaL_optlstring at 0x84FBD0 — validates optional string argument.
// 16 static callers. Same as luaL_checklstring but returns default when absent.
// Fast path: direct TString read for already-string TValue (tt==4).

typedef const char*(__cdecl *optstr_fn)(uintptr_t L, int narg, const char* def, uint32_t* len_out);
static optstr_fn orig = nullptr;
static volatile long g_hits = 0, g_misses = 0;

static const char* __cdecl hook(uintptr_t L, int narg, const char* def, uint32_t* len_out) {
    typedef uintptr_t(__cdecl *index2adr_fn)(int, uintptr_t);
    uintptr_t tv = ((index2adr_fn)0x0084D9C0)(narg, L);
    if (tv < 0x10000) return orig(L, narg, def, len_out);

    int tt = *(int*)(tv + 8);
    if (tt == 4) {
        uintptr_t ts = *(uintptr_t*)(tv + 0);
        if (ts > 0x10000) {
            *len_out = *(uint32_t*)(ts + 16);
            g_hits++;
            return (const char*)(ts + 20);
        }
    }
    g_misses++;
    return orig(L, narg, def, len_out);
}

bool InstallLuaOptstringFast() {
    void* t = (void*)0x0084FBD0;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    MH_EnableHook(t);
    Log("[OptStr] ACTIVE — luaL_optlstring inline at 0x84FBD0");
    CrashDumper::RegisterFeature("OptStr");
    CrashDumper::FeatureSetActive("OptStr", true);
    return true;
}
