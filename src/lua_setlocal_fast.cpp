#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

// NOTICE: 0x84F210 is NOT lua_setlocal.
// IDA decompile confirms sub_84F210(int L, int flag) is a 2-arg debug location
// formatter used by error functions (luaL_error / luaL_argerror).
// It calls lua_getstack + lua_getinfo("Sl") and formats "%s:%d: " (source:line).
// The original hook assumed a 3-arg lua_setlocal signature and corrupted the
// stack on every engine error, causing ntdll heap corruption at login.
//
// The REAL lua_getlocal is at 0x84FF30.
// The real lua_setlocal address is currently UNKNOWN — it is near 0x84FF30
// with the same ar->i_ci+96 pattern but copies L->top-1 to ci->base+n-1
// and decrements L->top (opposite direction of lua_getlocal).
//
// DO NOT re-enable this hook at 0x84F210 under any circumstances.
// To implement a real lua_setlocal fast path, first verify the correct
// engine address via IDA decompile, then rewrite this file entirely.

// Correct 2-arg signature for the actual function at 0x84F210
typedef const char*(__cdecl *debug_loc_fn)(uintptr_t L, unsigned int flag);
static debug_loc_fn orig = nullptr;
static volatile long g_hits = 0;

// Passthrough with correct 2-arg signature — safe, does nothing
static const char* __cdecl hook(uintptr_t L, unsigned int flag) {
    g_hits++;
    return orig(L, flag);
}

bool InstallLuaSetLocalFast() {
    // Block installation: 0x84F210 is NOT lua_setlocal.
    // If this were enabled, the wrong-address hook would corrupt the stack
    // on every engine call to the debug formatter, causing ntdll heap corruption.
#if TEST_DISABLE_LUA_SETLOCAL_FAST
    Log("[SetLocal] PERMANENTLY DISABLED: 0x84F210 is a debug formatter, NOT lua_setlocal");
    CrashDumper::RegisterFeature("SetLocal");
    CrashDumper::FeatureSetActive("SetLocal", false);
    return false;
#else
    // This code path should never be reached unless the flag is flipped to 0
    // (which would be incorrect — see notice above).
    void* t = (void*)0x0084F210;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    MH_EnableHook(t);
    Log("[SetLocal] ACTIVE at 0x84F210 — WARNING: this is NOT lua_setlocal");
    CrashDumper::RegisterFeature("SetLocal");
    CrashDumper::FeatureSetActive("SetLocal", true);
    return true;
#endif
}
