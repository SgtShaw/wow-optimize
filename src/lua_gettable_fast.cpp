#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"
#include "lua_index2adr.h"

extern "C" void Log(const char* fmt, ...);

#define LUA_TSTRING           4
#define TAINT_CELL 0x00D4139C
#define TAINT_A0   0x00D413A0
#define TAINT_A4   0x00D413A4

// lua_gettable at 0x84E560 — value = table[idx]
// key at L->top-16, result replaces key in-place
typedef int(__cdecl *gettable_fn)(uintptr_t L, int idx);
static gettable_fn orig = nullptr;
static volatile long g_hits = 0, g_misses = 0;

static int __cdecl hook(uintptr_t L, int idx) {
    if (L < 0x10000 || L > 0xBFFF0000) { g_misses++; return orig(L, idx); }

    uintptr_t top = *(uintptr_t*)(L + 0x0C);
    if (top < 0x10000 || top > 0xBFFF0000) { g_misses++; return orig(L, idx); }

    uintptr_t key_tv = top - 16;
    if (key_tv < 0x10000) { g_misses++; return orig(L, idx); }

    __try {
        int key_tt = *(int*)(key_tv + 8);
        if (key_tt != LUA_TSTRING) { g_misses++; return orig(L, idx); }

        // Resolve table via index2adr for pseudo-index compatibility
        uintptr_t table_tv = WowIndex2Adr(idx, L);
        if (table_tv < 0x10000 || *(int*)(table_tv + 8) != 5) { g_misses++; return orig(L, idx); }

        uintptr_t table = *(uintptr_t*)table_tv;
        if (table < 0x10000) { g_misses++; return orig(L, idx); }

        uintptr_t key_str = *(uintptr_t*)key_tv;
        if (key_str < 0x10000) { g_misses++; return orig(L, idx); }

        // luaH_getstr: lookup string key in table hash
        typedef uintptr_t(__cdecl *getstr_fn)(uintptr_t, uintptr_t);
        uintptr_t node = ((getstr_fn)0x0085C430)(table, key_str);
        if (node < 0x10000 || node == 0x00A46F78) { g_misses++; return orig(L, idx); }

        // If the value in the table is nil, defer to the engine to check metatables / __index
        if (*(int*)(node + 8) == 0) { g_misses++; return orig(L, idx); }

        // Copy the found value into the key's stack slot (in-place replacement)
        *(uint64_t*)(key_tv + 0) = *(uint64_t*)(node + 0);
        *(uint32_t*)(key_tv + 8) = *(uint32_t*)(node + 8);
        *(uint32_t*)(key_tv + 12) = *(uint32_t*)(node + 12);

        // Taint propagation from the value
        uint32_t val_taint = *(uint32_t*)(node + 12);
        if (val_taint && *(uint32_t*)TAINT_A0 && !*(uint32_t*)TAINT_A4)
            *(uint32_t*)TAINT_CELL = val_taint;

        g_hits++;
        return (int)key_tv;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    g_misses++;
    return orig(L, idx);
}

bool InstallLuaGetTableFast() {
    void* t = (void*)0x0084E560;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    WO_EnableHook(t);
    Log("[GetTable] ACTIVE — lua_gettable inline at 0x84E560");
    CrashDumper::RegisterFeature("GetTable");
    CrashDumper::FeatureSetActive("GetTable", true);
    return true;
}
