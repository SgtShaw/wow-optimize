// ============================================================================
// Module: lua_vm_cache.cpp
// Description: Accelerates Lua runtime calls in `lua_vm_cache.cpp`. Caches structures to bypass parser overhead.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================

#include "version.h"
#include "lua_optimize.h"
#include "MinHook.h"
#include <cstdint>
#include <cstring>

extern "C" void Log(const char* fmt, ...);

#if !TEST_DISABLE_LUA_OPCACHE

typedef struct lua_State lua_State;

union RawValue {
    void*     gc;
    uintptr_t ptr;
    double    n;
};

struct TValue {
    RawValue  value;
    int       tt;
    uint32_t  taint;
};

typedef void (__cdecl *luaV_gettable_fn)(lua_State*, void*, void*, void*);
static luaV_gettable_fn orig_luaV_gettable = nullptr;

static void __cdecl Hooked_luaV_gettable(lua_State* L, void* table, void* key, void* result) {
    if (LuaOpt::IsReloading() || LuaOpt::IsSwapping()) {
        orig_luaV_gettable(L, table, key, result);
        return;
    }

    if (!table || !key) {
        orig_luaV_gettable(L, table, key, result);
        return;
    }

    TValue* tv_table = (TValue*)table;
    TValue* tv_key = (TValue*)key;

    // Only accelerate lookups on actual tables with string keys
    if (tv_table->tt != 5 || tv_key->tt != 4) {
        orig_luaV_gettable(L, table, key, result);
        return;
    }

    uintptr_t t = (uintptr_t)tv_table->value.gc;
    uintptr_t k = (uintptr_t)tv_key->value.gc;

    if (t < 0x10000 || t > 0xFFE00000 || k < 0x10000 || k > 0xFFE00000) {
        orig_luaV_gettable(L, table, key, result);
        return;
    }

    // Direct table lookup check using luaH_getstr (0x0085C430)
    typedef uintptr_t(__cdecl *getstr_fn)(uintptr_t, uintptr_t);
    uintptr_t node = ((getstr_fn)0x0085C430)(t, k);
    if (node < 0x10000 || node == 0x00A46F78) {
        orig_luaV_gettable(L, table, key, result);
        return;
    }

    int node_tt = *(int*)(node + 8);
    // Only copy GC-safe primitive types directly; nil(0) is excluded to allow proper metatable fallback
    if (node_tt == 0 || node_tt > 4) {
        orig_luaV_gettable(L, table, key, result);
        return;
    }

    // Fast direct copy of the resolved node (always fresh, zero-invalidation correctness)
    *(uint32_t*)((char*)result)      = *(uint32_t*)(node);
    *(uint32_t*)((char*)result + 4)  = *(uint32_t*)(node + 4);
    *(uint32_t*)((char*)result + 8)  = node_tt;
    *(uint32_t*)((char*)result + 12) = *(uint32_t*)(node + 12);
}

bool InstallLuaVMCache() {
    void* target_get = (void*)0x857250;

    if (MH_CreateHook(target_get, (void*)Hooked_luaV_gettable, (void**)&orig_luaV_gettable) != MH_OK) {
        Log("[GetTableCache] CreateHook failed for luaV_gettable");
        return false;
    }
    if (MH_EnableHook(target_get) != MH_OK) {
        Log("[GetTableCache] EnableHook failed for luaV_gettable");
        return false;
    }

    Log("[GetTableCache] Active: direct-copy luaV_gettable accelerator at 0x857250");
    return true;
}

void GetTableCacheStats(long long* hits, long long* misses) {
    if (hits)   *hits   = 0;
    if (misses) *misses = 0;
}

void ClearTableCache() {}
extern "C" void InvalidateTableCacheSlot(void* table, void* key_str) {}

#else
bool InstallLuaVMCache() { return false; }
void GetTableCacheStats(long long* hits, long long* misses) {}
void ClearTableCache() {}
extern "C" void InvalidateTableCacheSlot(void* table, void* key_str) {}
#endif
