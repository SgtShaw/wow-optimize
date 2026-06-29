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

static constexpr int CACHE_SIZE  = 4096;
static constexpr int CACHE_MASK  = CACHE_SIZE - 1;

struct CacheSlot {
    union {
        struct {
            uint32_t table_tag;  // lower bits of table pointer
            uint32_t key_tag;    // lower bits of key (TString*)
        };
        uint64_t combined;
    };
    struct {
        uint32_t lo, hi;         // TValue data (double or pointer)
    } value;
    uint32_t type_tag;           // TValue type
};

static CacheSlot g_cache[CACHE_SIZE] = {};
static SRWLOCK  g_cacheLock = SRWLOCK_INIT;

static volatile LONG64 g_hits   = 0;
static volatile LONG64 g_misses = 0;

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

typedef void (__cdecl *luaV_settable_fn)(lua_State*, void*, void*, void*);
static luaV_settable_fn orig_luaV_settable = nullptr;

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

    // Hash: XOR mix of table and key GC object pointers
    uint32_t hash = (uint32_t)((t ^ (t >> 16) ^ k ^ (k >> 14)) & CACHE_MASK);
    uint64_t combined = ((uint64_t)(uint32_t)t << 32) | (uint32_t)k;

    AcquireSRWLockShared(&g_cacheLock);
    if (g_cache[hash].combined == combined) {
        uint32_t tp = g_cache[hash].type_tag;
        // Only cache GC-safe primitive types: nil(0),bool(1),lightuserdata(2),number(3),string(4)
        if (tp <= 4) {
            *(uint32_t*)((char*)result)      = g_cache[hash].value.lo;
            *(uint32_t*)((char*)result + 4)  = g_cache[hash].value.hi;
            *(uint32_t*)((char*)result + 8)  = tp;
            *(uint32_t*)((char*)result + 12) = 0;
            ReleaseSRWLockShared(&g_cacheLock);
            ++g_hits;
            return;
        }
    }
    ReleaseSRWLockShared(&g_cacheLock);

    ++g_misses;
    orig_luaV_gettable(L, table, key, result);

    // Cache only primitive results
    uint32_t tp = *(uint32_t*)((char*)result + 8);
    if (tp <= 4) {
        AcquireSRWLockExclusive(&g_cacheLock);
        g_cache[hash].combined  = combined;
        g_cache[hash].value.lo  = *(uint32_t*)((char*)result);
        g_cache[hash].value.hi  = *(uint32_t*)((char*)result + 4);
        g_cache[hash].type_tag  = tp;
        ReleaseSRWLockExclusive(&g_cacheLock);
    }
}

static void __cdecl Hooked_luaV_settable(lua_State* L, void* table, void* key, void* val) {
    if (table && key) {
        TValue* tv_table = (TValue*)table;
        TValue* tv_key = (TValue*)key;
        if (tv_table->tt == 5 && tv_key->tt == 4) {
            uintptr_t t = (uintptr_t)tv_table->value.gc;
            uintptr_t k = (uintptr_t)tv_key->value.gc;
            if (t >= 0x10000 && t <= 0xFFE00000 && k >= 0x10000 && k <= 0xFFE00000) {
                uint32_t hash = (uint32_t)((t ^ (t >> 16) ^ k ^ (k >> 14)) & CACHE_MASK);
                AcquireSRWLockExclusive(&g_cacheLock);
                g_cache[hash].combined = 0; // invalidate slot on write
                ReleaseSRWLockExclusive(&g_cacheLock);
            }
        }
    }
    orig_luaV_settable(L, table, key, val);
}

bool InstallLuaVMCache() {
    void* target_get = (void*)0x857250;
    void* target_set = (void*)0x8573C0;

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

    Log("[GetTableCache] Active: %d-slot cache on luaV_gettable/luaV_settable", CACHE_SIZE);
    return true;
}

void GetTableCacheStats(long long* hits, long long* misses) {
    if (hits)   *hits   = g_hits;
    if (misses) *misses = g_misses;
}

void ClearTableCache() {
    AcquireSRWLockExclusive(&g_cacheLock);
    memset(g_cache, 0, sizeof(g_cache));
    ReleaseSRWLockExclusive(&g_cacheLock);
}

#else
bool InstallLuaVMCache() { return false; }
void GetTableCacheStats(long long* hits, long long* misses) {}
void ClearTableCache() {}
#endif
