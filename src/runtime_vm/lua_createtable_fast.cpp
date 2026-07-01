// ============================================================================
// Module: lua_createtable_fast.cpp
// Description: Accelerates Lua runtime calls in `lua_createtable_fast.cpp`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================

#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

#define TAINT_CELL 0x00D4139C

// lua_createtable at 0x84E6E0 — create table (L, narray, nhash)
// Fast path: narray==0 && nhash==0 (most common addon pattern)
typedef int(__cdecl *createtable_fn)(uintptr_t L, int narray, int nhash);
static createtable_fn orig = nullptr;
static volatile long g_hits = 0, g_misses = 0;

static int __cdecl hook(uintptr_t L, int narray, int nhash) {
    if (L < 0x10000 || L > 0xFFE00000) { g_misses++; return orig(L, narray, nhash); }

    // Only optimize the trivial (0,0) case
    if (narray != 0 || nhash != 0) { g_misses++; return orig(L, narray, nhash); }

    __try {
        // GC check
        uintptr_t g = *(uintptr_t*)(L + 0x14);
        if (*(uintptr_t*)(g + 0x44) >= *(uintptr_t*)(g + 0x40)) {
            typedef void(__cdecl *gcstep_fn)(uintptr_t);
            ((gcstep_fn)0x0085B950)(L);
        }

        // Allocate table via luaH_new (0 arrays, 0 hash)
        typedef uintptr_t(__cdecl *newtable_fn)(uintptr_t, int, int);
        uintptr_t table = ((newtable_fn)0x0085C2E0)(L, 0, 0);
        if (table < 0x10000) { g_misses++; return orig(L, narray, nhash); }

        // Push onto stack: tt=5 (LUA_TTABLE)
        uintptr_t new_top = *(uintptr_t*)(L + 0x0C);
        if (new_top < 0x10000 || new_top > 0xFFE00000) { g_misses++; return orig(L, narray, nhash); }
        uint32_t taint = *(uint32_t*)TAINT_CELL;
        *(uintptr_t*)(new_top + 0) = table;
        *(uint32_t*)(new_top + 4) = 0;
        *(uint32_t*)(new_top + 8) = 5;
        *(uint32_t*)(new_top + 12) = taint;
        *(uintptr_t*)(L + 0x0C) = new_top + 16;

        g_hits++;
        return (int)table;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    g_misses++;
    return orig(L, narray, nhash);
}

bool InstallLuaCreateTableFast() {
    void* t = (void*)0x0084E6E0;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    WO_EnableHook(t);
    Log("[CreateTable] ACTIVE — lua_createtable inline at 0x84E6E0");
    CrashDumper::RegisterFeature("CreateTable");
    CrashDumper::FeatureSetActive("CreateTable", true);
    return true;
}
