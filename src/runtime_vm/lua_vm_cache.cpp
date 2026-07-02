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

#include <windows.h>

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

static constexpr int CACHE_SIZE  = 4096;
static constexpr int CACHE_MASK  = CACHE_SIZE - 1;

struct CacheSlot {
    uint64_t combined;           // ((uint64_t)table << 32) | key
    struct {
        uint32_t lo, hi;         // TValue data (double or pointer)
    } value;
    uint32_t type_tag;           // TValue type
    uint32_t taint;              // WoW UI taint tracking
};

static CacheSlot g_cache[CACHE_SIZE] = {};
static SRWLOCK   g_cacheLock = SRWLOCK_INIT;

static volatile LONG64 g_hits   = 0;
static volatile LONG64 g_misses = 0;

typedef void (__cdecl *luaV_gettable_fn)(lua_State*, void*, void*, void*);
static luaV_gettable_fn orig_luaV_gettable = nullptr;

typedef void (__cdecl *luaV_settable_fn)(lua_State*, void*, void*, void*);
static luaV_settable_fn orig_luaV_settable = nullptr;

typedef void (__cdecl *luaC_step_fn)(lua_State*);
static luaC_step_fn orig_luaC_step = nullptr;

static void __cdecl Hooked_luaV_gettable(lua_State* L, void* table, void* key, void* result) {
    if (LuaOpt::IsReloading() || LuaOpt::IsSwapping()) {
        orig_luaV_gettable(L, table, key, result);
        return;
    }

    if (!table || !key || !result) {
        orig_luaV_gettable(L, table, key, result);
        return;
    }

    TValue* tv_table = (TValue*)table;
    TValue* tv_key = (TValue*)key;

    // Only cache lookups on actual tables with string keys
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

    uint64_t combined = ((uint64_t)(uint32_t)t << 32) | (uint32_t)k;
    uint32_t hash = (uint32_t)((t ^ (t >> 16) ^ k ^ (k >> 14)) & CACHE_MASK);

    AcquireSRWLockShared(&g_cacheLock);
    if (g_cache[hash].combined == combined) {
        uint32_t tp = g_cache[hash].type_tag;
        *(uint32_t*)((char*)result)      = g_cache[hash].value.lo;
        *(uint32_t*)((char*)result + 4)  = g_cache[hash].value.hi;
        *(uint32_t*)((char*)result + 8)  = tp;
        *(uint32_t*)((char*)result + 12) = g_cache[hash].taint;
        ReleaseSRWLockShared(&g_cacheLock);
        InterlockedIncrement64(&g_hits);
        return;
    }
    ReleaseSRWLockShared(&g_cacheLock);

    // Cache miss: use standard engine getstr (0x0085C430) to resolve
    typedef uintptr_t(__cdecl *getstr_fn)(uintptr_t, uintptr_t);
    uintptr_t node = ((getstr_fn)0x0085C430)(t, k);
    if (node < 0x10000 || node == 0x00A46F78) {
        orig_luaV_gettable(L, table, key, result);
        return;
    }

    int node_tt = *(int*)(node + 8);
    // Only cache GC-safe primitive types; nil(0) is excluded to allow proper metatable fallback
    if (node_tt == 0 || node_tt > 4) {
        orig_luaV_gettable(L, table, key, result);
        return;
    }

    InterlockedIncrement64(&g_misses);

    // Copy to result
    *(uint32_t*)((char*)result)      = *(uint32_t*)(node);
    *(uint32_t*)((char*)result + 4)  = *(uint32_t*)(node + 4);
    *(uint32_t*)((char*)result + 8)  = node_tt;
    *(uint32_t*)((char*)result + 12) = *(uint32_t*)(node + 12);

    // Save to cache
    AcquireSRWLockExclusive(&g_cacheLock);
    g_cache[hash].combined  = combined;
    g_cache[hash].value.lo  = *(uint32_t*)((char*)result);
    g_cache[hash].value.hi  = *(uint32_t*)((char*)result + 4);
    g_cache[hash].type_tag  = node_tt;
    g_cache[hash].taint     = *(uint32_t*)((char*)result + 12);
    ReleaseSRWLockExclusive(&g_cacheLock);
}

extern "C" void InvalidateTableCacheSlot(void* table, void* key_str) {
    if (!table || !key_str) return;
    uintptr_t t = (uintptr_t)table;
    uintptr_t k = (uintptr_t)key_str;
    if (t < 0x10000 || k < 0x10000) return;

    uint32_t hash = (uint32_t)((t ^ (t >> 16) ^ k ^ (k >> 14)) & CACHE_MASK);
    uint64_t combined = ((uint64_t)(uint32_t)t << 32) | (uint32_t)k;

    AcquireSRWLockExclusive(&g_cacheLock);
    if (g_cache[hash].combined == combined) {
        g_cache[hash].combined = 0;
    }
    ReleaseSRWLockExclusive(&g_cacheLock);
}

static void __cdecl Hooked_luaV_settable(lua_State* L, void* table, void* key, void* value) {
    if (table && key) {
        TValue* tv_table = (TValue*)table;
        TValue* tv_key = (TValue*)key;
        if (tv_table->tt == 5 && tv_key->tt == 4) { // Table and String key
            uintptr_t t = (uintptr_t)tv_table->value.gc;
            uintptr_t k = (uintptr_t)tv_key->value.gc;
            if (t >= 0x10000 && k >= 0x10000) {
                InvalidateTableCacheSlot((void*)t, (void*)k);
            }
        }
    }
    orig_luaV_settable(L, table, key, value);
}

static void __cdecl Hooked_luaC_step(lua_State* L) {
    // Clear entire cache on GC sweeps to prevent UAF/stale GC object addresses
    AcquireSRWLockExclusive(&g_cacheLock);
    std::memset(g_cache, 0, sizeof(g_cache));
    ReleaseSRWLockExclusive(&g_cacheLock);
    orig_luaC_step(L);
}

bool InstallLuaVMCache() {
    void* target_get = (void*)0x857250;
    void* target_set = (void*)0x8573C0;
    void* target_gc  = (void*)0x85B950;

    if (MH_CreateHook(target_get, (void*)Hooked_luaV_gettable, (void**)&orig_luaV_gettable) != MH_OK) {
        Log("[GetTableCache] CreateHook failed for luaV_gettable");
        return false;
    }
    if (MH_EnableHook(target_get) != MH_OK) {
        Log("[GetTableCache] EnableHook failed for luaV_gettable");
        return false;
    }

    if (MH_CreateHook(target_set, (void*)Hooked_luaV_settable, (void**)&orig_luaV_settable) != MH_OK) {
        Log("[GetTableCache] CreateHook failed for luaV_settable");
        return false;
    }
    if (MH_EnableHook(target_set) != MH_OK) {
        Log("[GetTableCache] EnableHook failed for luaV_settable");
        return false;
    }

    if (MH_CreateHook(target_gc, (void*)Hooked_luaC_step, (void**)&orig_luaC_step) != MH_OK) {
        Log("[GetTableCache] CreateHook failed for luaC_step");
        return false;
    }
    if (MH_EnableHook(target_gc) != MH_OK) {
        Log("[GetTableCache] EnableHook failed for luaC_step");
        return false;
    }

    Log("[GetTableCache] Active: %d-slot synchronized VM table cache (synced via settable/GC)", CACHE_SIZE);
    return true;
}

void GetTableCacheStats(long long* hits, long long* misses) {
    if (hits)   *hits   = g_hits;
    if (misses) *misses = g_misses;
}

void ClearTableCache() {
    AcquireSRWLockExclusive(&g_cacheLock);
    std::memset(g_cache, 0, sizeof(g_cache));
    ReleaseSRWLockExclusive(&g_cacheLock);
}

#else
bool InstallLuaVMCache() { return false; }
void GetTableCacheStats(long long* hits, long long* misses) {}
void ClearTableCache() {}
extern "C" void InvalidateTableCacheSlot(void* table, void* key_str) {}
#endif
