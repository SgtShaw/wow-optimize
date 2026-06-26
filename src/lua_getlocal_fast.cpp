// lua_getlocal at 0x84F0F0
// IDA-verified signature: __cdecl (lua_State* L, int n)
// NOT (L, ar, n) — there is no lua_Debug* parameter at this address.
//
// Engine does:
//   1. GC threshold check + luaC_step if needed
//   2. Determines current closure env (base CI vs current fn env at offset +16)
//   3. sub_856DD0(L, n, env) — looks up local name by index in the Proto
//   4. Pushes result onto stack as tt=7 (LUA_TUSERDATA-like; actually upvalue ptr)
//   5. Returns v4 + 24 (pointer to local name string within the Proto)
//
// This is NOT a simple "copy local slot to top" function. It walks the Proto's
// local variable list to get the name, then pushes the value. The original
// interacts heavily with the closure/proto structure.
//
// Given the complexity and the wrong-signature crash history, this hook is
// left as a thin passthrough to the original until the Proto local-var walk
// is reverse-engineered and verified field-by-field.

#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

// Actual signature: (lua_State* L, int n) — 2 args only.
typedef const char*(__cdecl *getlocal_fn)(uintptr_t L, int n);
static getlocal_fn orig = nullptr;

bool InstallLuaGetLocalFast() {
    void* t = (void*)0x0084F0F0;
    if (MH_CreateHook(t, orig, (void**)&orig) != MH_OK) return false;
    // Hook deliberately not enabled — passthrough until proto walk is verified.
    Log("[GetLocal] PASSTHROUGH only — hook body needs re-implementation (wrong sig fixed)");
    CrashDumper::RegisterFeature("GetLocal");
    return true;
}
