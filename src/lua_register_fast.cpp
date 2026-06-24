#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

#define TAINT_CELL 0x00D4139C

// luaL_register at 0x84FC00 — register C functions into a module table
// Fast path: inline the function-registration loop (most common case)
// Falls back to original for name conflicts, upvalues, NULL name, etc.
typedef int(__cdecl *register_fn)(uintptr_t L, const char* name, uintptr_t* funcs, int nup);
static register_fn orig = nullptr;
static volatile long g_hits = 0, g_misses = 0;

static int __cdecl hook(uintptr_t L, const char* name, uintptr_t* funcs, int nup) {
    if (L < 0x10000 || L > 0xBFFF0000) { g_misses++; return orig(L, name, funcs, nup); }
    if (!name || !funcs || nup != 0) { g_misses++; return orig(L, name, funcs, nup); }

    __try {
        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        uintptr_t base = *(uintptr_t*)(L + 0x10);
        int32_t taint = *(int32_t*)TAINT_CELL;

        // Table must be at top of stack (tt == 5 = LUA_TTABLE)
        uintptr_t tableSlot = top - 16;
        if (tableSlot < base) { g_misses++; return orig(L, name, funcs, nup); }
        uint32_t tt = *(uint32_t*)(tableSlot + 8);
        if (tt != 5) { g_misses++; return orig(L, name, funcs, nup); }
        uintptr_t table = *(uintptr_t*)tableSlot;

        // Register each function
        uintptr_t* pair = funcs;
        while (pair[0]) {
            const char* fname = (const char*)pair[0];
            uintptr_t cfunc = pair[1];
            if (!fname || !cfunc) break;

            // Push string: call engine's lua_pushstring
            typedef uintptr_t(__cdecl *pushstr_fn)(uintptr_t, const char*);
            uintptr_t str = ((pushstr_fn)0x0084E400)(L, fname);
            if (!str) break;

            // Push C closure: call engine's lua_pushcclosure
            typedef void(__cdecl *pushccl_fn)(uintptr_t, uintptr_t, int);
            ((pushccl_fn)0x0084E590)(L, cfunc, 0);

            // Set field in table: call engine's lua_setfield
            typedef void(__cdecl *setfield_fn)(uintptr_t, int, const char*);
            ((setfield_fn)0x0084E900)(L, -2, fname);

            pair += 2;
        }

        // Leave table on stack (it was at top, now it's at top)
        g_hits++;
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    g_misses++;
    return orig(L, name, funcs, nup);
}

bool InstallLuaRegisterFast() {
    void* t = (void*)0x0084FC00;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    WO_EnableHook(t);
    Log("[LRegister] ACTIVE — luaL_register inline at 0x84FC00");
    CrashDumper::RegisterFeature("LRegister");
    CrashDumper::FeatureSetActive("LRegister", true);
    return true;
}
