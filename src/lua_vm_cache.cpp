// Lua VM table lookup cache
// Hooks luaV_gettable (0x857250). Caches all table[const] lookups.
// Massive hit rate for global variables (UnitHealth, GetTime, etc.)
// since they're looked up by the same TString* every call.

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

typedef void (__cdecl *luaV_gettable_fn)(void*, void*, void*, void*);
static luaV_gettable_fn orig_luaV_gettable = nullptr;

static void __cdecl Hooked_luaV_gettable(void* L, void* table, void* key, void* result) {
    // Skip cache during lua_State swap — old table/key pointers are stale
    if (LuaOpt::IsReloading() || LuaOpt::IsSwapping()) {
        orig_luaV_gettable(L, table, key, result);
        return;
    }

    if (!key) { orig_luaV_gettable(L, table, key, result); return; }

    uintptr_t t = (uintptr_t)table;
    uintptr_t k = (uintptr_t)key;

    // Hash: XOR mix of table and key pointers
    uint32_t hash = (uint32_t)((t ^ (t >> 16) ^ k ^ (k >> 14)) & CACHE_MASK);
    uint64_t combined = ((uint64_t)(uint32_t)t << 32) | (uint32_t)k;

    AcquireSRWLockShared(&g_cacheLock);
    if (g_cache[hash].combined == combined) {
        uint32_t tp = g_cache[hash].type_tag;
        // Only cache GC-safe types: nil(0),bool(1),lightuserdata(2),number(3),string(4)
        // Tables(5)/functions(6)/userdata(7)/thread(8) can be collected between calls.
        if (tp <= 4) {
            *(uint32_t*)((char*)result)      = g_cache[hash].value.lo;
            *(uint32_t*)((char*)result + 4)  = g_cache[hash].value.hi;
            *(uint32_t*)((char*)result + 8)  = tp;
            *(uint32_t*)((char*)result + 12) = 0;
            ReleaseSRWLockShared(&g_cacheLock);
            InterlockedIncrement64(&g_hits);
            return;
        }
    }
    ReleaseSRWLockShared(&g_cacheLock);

    InterlockedIncrement64(&g_misses);
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

bool InstallLuaVMCache() {
    void* target = (void*)0x857250;
    if (MH_CreateHook(target, (void*)Hooked_luaV_gettable,
                      (void**)&orig_luaV_gettable) != MH_OK) {
        Log("[GetTableCache] CreateHook failed at 0x857250");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[GetTableCache] EnableHook failed");
        return false;
    }
    Log("[GetTableCache] Active: %d-slot cache on luaV_gettable", CACHE_SIZE);
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
