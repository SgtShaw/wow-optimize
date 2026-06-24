#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

// lua_tolstring at 0x84E0E0 — converts value to string.
// 100+ static callers, millions/sec in UI layout/addon loops.
// Fast path: for already-string TValues (tt==4), read TString pointer + length directly.

typedef const char*(__cdecl *tolstr_fn)(uintptr_t L, int idx, uint32_t* len_out);
static tolstr_fn orig = nullptr;
static volatile long g_hits = 0, g_misses = 0;

static const char* __cdecl hook(uintptr_t L, int idx, uint32_t* len_out) {
    typedef uintptr_t(__cdecl *index2adr_fn)(int, uintptr_t);
    uintptr_t tv = ((index2adr_fn)0x0084D9C0)(idx, L);
    if (tv < 0x10000) return orig(L, idx, len_out);

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
    return orig(L, idx, len_out);
}

bool InstallLuaTolstringFast() {
    void* t = (void*)0x0084E0E0;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    MH_EnableHook(t);
    Log("[TolStr] ACTIVE — lua_tolstring inline at 0x84E0E0");
    CrashDumper::RegisterFeature("TolStr");
    CrashDumper::FeatureSetActive("TolStr", true);
    return true;
}
