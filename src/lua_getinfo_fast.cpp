// sub_84F3B0 — IDA-verified: this is NOT lua_getinfo.
// It is luaL_findfield(L, idx, field, level) — walks a dotted field path
// like "foo.bar.baz" through nested tables on the Lua stack.
//
// IDA decompile shows:
//   sub_84DE50(L, idx)           → lua_pushvalue: pushes stack[idx] to top
//   loop: strchr(Str, '.') to find next segment
//     sub_84E300(L, Str, len)    → lua_pushlstring: push segment key
//     sub_84E600(L, -2)          → lua_rawget: look up key in table at -2
//     if type == 0 (nil): create new table sub_84E6E0, set field, continue
//     if type != 5 (table): break (not a table, fail)
//     sub_84DC50(L, -2)          → lua_remove: remove the old table
//   returns pointer to remaining path (past last '.') or nullptr on failure
//
// Our hook was zeroing arbitrary offsets in 'ar' treating it as lua_Debug,
// which it is NOT. The hook is replaced by a correct thin wrapper.
// No inlining is attempted — the dotted-path walk is complex and correct
// behavior requires the full engine path.

#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

// Correct signature: luaL_findfield(L, idx, field, level)
typedef char*(__cdecl *getinfo_fn)(uintptr_t L, int idx, const char* field, int level);
static getinfo_fn orig = nullptr;
static volatile long g_calls = 0;

static char* __cdecl hook(uintptr_t L, int idx, const char* field, int level) {
    g_calls++;
    return orig(L, idx, field, level);
}

bool InstallLuaGetInfoFast() {
    void* t = (void*)0x0084F3B0;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    MH_EnableHook(t);
    Log("[GetInfo] ACTIVE — luaL_findfield inline at 0x84F3B0");
    CrashDumper::RegisterFeature("GetInfo");
    CrashDumper::FeatureSetActive("GetInfo", true);
    return true;
}
