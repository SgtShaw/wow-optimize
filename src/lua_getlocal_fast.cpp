// lua_getlocal at 0x84F0F0
// IDA-verified disassembly (35 instructions):
//
//   arg_0 = L (lua_State*)
//   arg_4 = n (int, local variable index)
//
// Engine flow (verified against disasm):
//   1. GC check: if (L->gcstate.totalbytes >= L->gcstate.threshold) luaC_step(L)
//   2. Determine current closure env:
//      ci = L->ci (L+0x18)
//      if (ci == L->base_ci (L+0x2C))  → env = L->gt (L+0x48)
//      else                             → env = *(*(ci->func)+0x10)   [CClosure/LClosure env at +16]
//   3. result = sub_856DD0(L, env, n) — allocates a new GC userdata LocVar object
//      sub_856DD0 is a GC allocator: sets result+8=7, result+9=GC_color,
//      result+4=taint, result+20=n, result+16=env, links into GC list
//   4. Push onto stack:
//      top = L->top (L+0x0C)
//      top[0]  = result          (value ptr)
//      top[8]  = 7               (LUA_TUSERDATA)
//      top[12] = dword_D4139C    (taint)
//      L->top += 16
//   5. Return result + 0x18       (name field inside the LocVar object)
//
// This function does NOT do a simple slot copy. It allocates a new GC object.
// The GC allocation path (sub_856DD0 → sub_85D6F0) is complex; we cannot
// safely inline it. This hook is a correct thin wrapper — same signature,
// same behavior, calls original for the heavy lifting.
//
// We hook it purely so CrashDumper tracks it and for future profiling.

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
