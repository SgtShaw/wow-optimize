#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

typedef const char*(__cdecl *setlocal_fn)(uintptr_t L, uintptr_t ar, int n);
static setlocal_fn orig = nullptr;
static volatile long g_hits = 0, g_misses = 0;

static const char* __cdecl hook(uintptr_t L, uintptr_t ar, int n) {
    g_misses++;
    return orig(L, ar, n);
}

bool InstallLuaSetLocalFast() {
    void* t = (void*)0x0084F210;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    MH_EnableHook(t);
    Log("[SetLocal] ACTIVE — lua_setlocal inline at 0x84F210");
    CrashDumper::RegisterFeature("SetLocal");
    CrashDumper::FeatureSetActive("SetLocal", true);
    return true;
}
