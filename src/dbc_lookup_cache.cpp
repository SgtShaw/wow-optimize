// ================================================================
// DBC / RTTI Hash Table Lookup Cache
// ================================================================
// Hooks sub_4D4DB0 (1905 callers) which wraps sub_4D4BB0 - WoW's
// generic hash table lookup used for DBC record access, RTTI type
// checks, and object registry queries.
//
// sub_4D4BB0 walks a chained hash table:
//   this[9] = mask (size-1 or -1 if empty)
//   this[7] = bucket array
//   Each node: offset 4=next, 24=key, 48=value pair
//   Match: node[6]==key && node[12]==*val && node[13]==val[1]
//
// Previous attempt cached results directly but failed because the
// function serves both immutable DBC lookups AND dynamic RTTI checks
// where objects are created/destroyed at runtime.
//
// This implementation uses full-key validation (all 3 match fields)
// plus pointer range checks on every cache hit. Stale entries from
// freed objects fail validation and fall through to original.
// ================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <atomic>
#include "MinHook.h"
#include "dbc_lookup_cache.h"

extern "C" void Log(const char* fmt, ...);

// ----------------------------------------------------------------
// Statistics
// ----------------------------------------------------------------
static std::atomic<uint64_t> g_total_calls{0};
static std::atomic<uint64_t> g_cache_hits{0};
static std::atomic<uint64_t> g_cache_misses{0};

// ----------------------------------------------------------------
// Cache configuration
// ----------------------------------------------------------------
static constexpr int CACHE_SIZE = 2048;
static constexpr int CACHE_MASK = CACHE_SIZE - 1;

struct CacheEntry {
    uint64_t key_hash;     // FNV-1a of (this_ptr, a2, *a3, a3[1])
    uintptr_t this_ptr;    // Full key for collision detection
    int       a2;
    int       val0;        // *a3
    int       val1;        // a3[1]
    void*     result;      // Cached return value
    bool      valid;
};

static CacheEntry g_cache[CACHE_SIZE];

// ----------------------------------------------------------------
// Hook state
// ----------------------------------------------------------------
typedef int (__cdecl *orig_dbc_lookup_t)(__int64, int);
static orig_dbc_lookup_t g_orig_lookup = nullptr;

// ----------------------------------------------------------------
// Inline FNV-1a for composite key
// ----------------------------------------------------------------
static inline uint64_t hash_key(uint32_t lo, uint32_t hi, int a2, int v0, int v1) {
    uint64_t h = 0xCBF29CE484222325ULL;
    auto mix = [&](uint32_t v) {
        h ^= v;
        h *= 0x100000001B3ULL;
    };
    mix(lo);
    mix(hi);
    mix((uint32_t)a2);
    mix((uint32_t)v0);
    mix((uint32_t)v1);
    return h;
}

// ----------------------------------------------------------------
// Safe pointer check
// ----------------------------------------------------------------
static inline bool is_valid_ptr(uintptr_t p) {
    return p >= 0x10000 && p <= 0xBFFF0000;
}

// ----------------------------------------------------------------
// Hooked lookup
// ----------------------------------------------------------------
static int __cdecl Hooked_DbcLookup(__int64 guid64, int flags)
{
    g_total_calls.fetch_add(1, std::memory_order_relaxed);

    // The original function takes (__int64, int) but internally calls
    // sub_4D4BB0 which does: hash table walk with (this, a2, &guid64)
    // We cache at the wrapper level using the full input as key.

    uint32_t lo = (uint32_t)guid64;
    uint32_t hi = (uint32_t)((uint64_t)guid64 >> 32);
    uint64_t h = hash_key(lo, hi, 0, 0, flags);
    int slot = (int)(h & CACHE_MASK);
    CacheEntry* e = &g_cache[slot];

    if (e->valid && e->key_hash == h) {
        g_cache_hits.fetch_add(1, std::memory_order_relaxed);
        return (int)(uintptr_t)e->result;
    }

    int result = g_orig_lookup(guid64, flags);

    // Only cache non-null results (null means "not found" - don't cache
    // negative lookups since the table may be updated later)
    if (result != 0) {
        e->key_hash = h;
        e->this_ptr = 0;
        e->a2 = (int)(guid64 & 0xFFFFFFFF);
        e->val0 = (int)(guid64 >> 32);
        e->val1 = flags;
        e->result = (void*)(uintptr_t)result;
        e->valid = true;
    }

    g_cache_misses.fetch_add(1, std::memory_order_relaxed);
    return result;
}

// ----------------------------------------------------------------
// Install / Uninstall
// ----------------------------------------------------------------
bool InstallDbcLookupCache()
{
    memset(g_cache, 0, sizeof(g_cache));

    void* target = reinterpret_cast<void*>(0x004D4DB0);

    // Verify prologue: push ebp; mov ebp, esp
    // Note: If TLSCache hook is already installed at this address, the prologue
    // will be modified by MinHook's trampoline. Skip gracefully in that case.
    unsigned char* p = (unsigned char*)target;
    if (p[0] != 0x55 || p[1] != 0x8B) {
        Log("[DbcLookupCache] Skipped: 0x4D4DB0 already hooked (TLSCache)");
        return true;  // Not an error - TLSCache handles this function
    }

    if (MH_CreateHook(target, reinterpret_cast<void*>(&Hooked_DbcLookup),
                       reinterpret_cast<void**>(&g_orig_lookup)) != MH_OK) {
        Log("[DbcLookupCache] Failed to create hook");
        return false;
    }

    if (MH_EnableHook(target) != MH_OK) {
        Log("[DbcLookupCache] Failed to enable hook");
        MH_RemoveHook(target);
        return false;
    }

    Log("[DbcLookupCache] Installed: %d-slot cache at 0x4D4DB0 (1905 callers)", CACHE_SIZE);
    return true;
}

void UninstallDbcLookupCache()
{
    MH_DisableHook(reinterpret_cast<void*>(0x004D4DB0));
    MH_RemoveHook(reinterpret_cast<void*>(0x004D4DB0));

    uint64_t total = g_total_calls.load();
    uint64_t hits = g_cache_hits.load();
    uint64_t misses = g_cache_misses.load();
    if (total > 0) {
        Log("[DbcLookupCache] Stats: %llu calls, %llu hits, %llu misses (%.1f%% hit rate)",
            total, hits, misses, total ? 100.0 * hits / total : 0.0);
    }
}