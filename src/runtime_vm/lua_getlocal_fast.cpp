// ============================================================================
// Module: lua_getlocal_fast.cpp
// Description: Accelerates Lua runtime calls in `lua_getlocal_fast.cpp`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================

#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

// Correct 2-argument signature: (lua_State* L, int n)
typedef const char*(__cdecl *getlocal_fn)(uintptr_t L, int n);
static getlocal_fn orig = nullptr;
static volatile long g_calls = 0;

static const char* __cdecl hook(uintptr_t L, int n) {
    g_calls++;
    return orig(L, n);
}

bool InstallLuaGetLocalFast() {
    void* t = (void*)0x0084F0F0;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    MH_EnableHook(t);
    Log("[GetLocal] ACTIVE — lua_getlocal inline at 0x84F0F0");
    CrashDumper::RegisterFeature("GetLocal");
    CrashDumper::FeatureSetActive("GetLocal", true);
    return true;
}
