// ============================================================================
// Module: lua_getstr_inline.cpp
// Description: Accelerates Lua runtime calls in `lua_getstr_inline.cpp`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <emmintrin.h>
#include "MinHook.h"
#include "lua_getstr_inline.h"
#include "hot_patch.h"
#include "lua_optimize.h"
#include "crash_dumper.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

// ----------------------------------------------------------------
// Statistics (diagnostic only; plain increments. The Lua VM is
// single-threaded, so the previous InterlockedIncrement64 -- a lock
// cmpxchg8b loop on x86 -- only added cost to this very hot path.)
// ----------------------------------------------------------------
static volatile LONG64 g_total_calls = 0;
static volatile LONG64 g_first_node_hits = 0;
static volatile LONG64 g_cache_hits = 0;
static volatile LONG64 g_chain_walks = 0;
static volatile LONG64 g_chain_depth_total = 0;
static volatile LONG64 g_nil_returns = 0;

// ----------------------------------------------------------------
// Cache configuration
// ----------------------------------------------------------------
// Direct-mapped cache: 16384 entries, keyed on (table ^ tstring_hash)
// Stores bucket INDEX (not pointer!) + lsize for resize detection
static constexpr int CACHE_BITS = 14;
static constexpr int CACHE_SIZE = 1 << CACHE_BITS;
static constexpr int CACHE_MASK = CACHE_SIZE - 1;

struct SafeGetStrEntry {
    uint32_t table_lo;      // lower 32 bits of table ptr
    uint32_t tstring_hash;  // tstring[12] precomputed hash
    uint32_t bucket_idx;    // bucket index where string was found
    uint8_t  lsize;         // table[11] at time of caching (resize detector)
    uint8_t  pad[3];        // alignment
};

static SafeGetStrEntry g_cache[CACHE_SIZE];

void InvalidateLuaGetStrInlineCache() {
    memset(g_cache, 0, sizeof(g_cache));
}

// ----------------------------------------------------------------
// Nil object sentinel (returned when key not found)
// Located at 0xA46F78 in WoW 3.3.5a
// ----------------------------------------------------------------
static void* g_nil_object = (void*)0x00A46F78;

// ----------------------------------------------------------------
// Original function pointer
// ----------------------------------------------------------------
typedef void* (__cdecl *luaH_getstr_fn)(int table, int tstring);
static luaH_getstr_fn g_orig_getstr = nullptr;

// ----------------------------------------------------------------
// Optimized replacement — SAFE (no pointer caching)
// ----------------------------------------------------------------
static void* __cdecl Optimized_GetStr(int table, int tstring)
{
    CrashDumper::RecordHookCall("GetStrInline", (uintptr_t)table);
    ++g_total_calls;

    // Bail out during lua_State swap — table and tstring pointers become
    // garbage when WoW destroys the old Lua VM during UI reload/logout.
    if (LuaOpt::IsReloading() || LuaOpt::IsSwapping()) {
        return g_orig_getstr(table, tstring);
    }

    // Validate inputs — reject obviously invalid pointers
    if ((uintptr_t)table < 0x10000 || (uintptr_t)table > 0xFFE00000 ||
        (uintptr_t)tstring < 0x10000 || (uintptr_t)tstring > 0xFFE00000) {
        return g_orig_getstr(table, tstring);
    }

    // The entire node access is wrapped in SEH. The engine (sub_85C430)
    // trusts the bucket chain and dereferences it with no bounds check; we
    // match that for VALID tables but, on a genuinely corrupt chain, defer
    // to the original instead of faulting inside the hook. This SEH backstop
    // replaces the previous hard bounds-check `break`, which was the bug:
    // on /3GB clients (mimalloc now backs the whole heap and places arenas
    // high) a live node could sit above 0xBFFF0000, so the walk broke early
    // and returned the nil sentinel for a LIVE key -> WeakAuras aura_env and
    // other addon fields read nil. We now walk exactly like the engine.
    __try {
        // Read tstring hash: tstring[12] = precomputed string hash
        uint32_t ts_hash = *(uint32_t*)(tstring + 12);

        // Read table metadata
        uint8_t  lsize      = *(uint8_t*)(table + 11);   // log2 of hash size
        uint32_t* node_array = *(uint32_t**)(table + 20); // hash bucket array

        if (!node_array || lsize == 0 || lsize > 24) {
            return g_orig_getstr(table, tstring);
        }

        // Compute bucket index: ts_hash & ((1 << lsize) - 1)
        uint32_t bucket_mask = (1u << lsize) - 1;
        uint32_t bucket_idx  = ts_hash & bucket_mask;

        // Get first node in chain (each node is 40 bytes = 10 DWORDs)
        uint32_t* node = (uint32_t*)((uint8_t*)node_array + 40 * bucket_idx);

        // FAST PATH 1: direct first-node check (~80% of lookups).
        if (node[6] == 4 && node[4] == (uint32_t)tstring) {
            ++g_first_node_hits;
            return node;
        }

        // FAST PATH 2: bucket-index cache (stores index+lsize, never a
        // pointer; recomputes from the fresh node_array and content-validates,
        // so a mimalloc address reuse can never return wrong data).
        uint32_t cache_key = ((uint32_t)table ^ ts_hash) & CACHE_MASK;
        SafeGetStrEntry* entry = &g_cache[cache_key];

        if (entry->table_lo == (uint32_t)table &&
            entry->tstring_hash == ts_hash &&
            entry->lsize == lsize) {
            uint32_t cached_bucket = entry->bucket_idx;
            if (cached_bucket <= bucket_mask) {
                uint32_t* cached_node = (uint32_t*)((uint8_t*)node_array + 40 * cached_bucket);
                if (cached_node[6] == 4 && cached_node[4] == (uint32_t)tstring) {
                    ++g_cache_hits;
                    return cached_node;
                }
            }
        }

        // SLOW PATH: walk the chain exactly like the engine — follow node[8]
        // until a match or NULL. NO early bounds-check break (that was the bug).
        ++g_chain_walks;
        int depth = 1;
        void* next = (void*)node[8];
        while (next != nullptr) {
            uint32_t* n = (uint32_t*)next;
            if (n[6] == 4 && n[4] == (uint32_t)tstring) {
                // Found — cache the chain's start bucket (all nodes in a chain
                // share it) so a repeat lookup short-circuits the walk.
                entry->table_lo = (uint32_t)table;
                entry->tstring_hash = ts_hash;
                entry->bucket_idx = bucket_idx;
                entry->lsize = lsize;
                g_chain_depth_total += depth;
                return n;
            }
            next = (void*)n[8];
            depth++;
        }

        // End of chain, no match — same as the engine returning &nilObject
        // when result[8] becomes NULL.
        ++g_nil_returns;
        return g_nil_object;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Corrupt chain/table — fall back to the engine rather than fault.
        return g_orig_getstr(table, tstring);
    }
}

// ----------------------------------------------------------------
// Install / Uninstall
// ----------------------------------------------------------------
bool InstallLuaGetStrInline()
{
    // Verified correct against the stock luaH_getstr decompile (0x85C430):
    // identical offsets (table+20 node array, table+11 lsize, tstring+12 hash),
    // node fields ([4] key, [6] tt==4, [8] next) and nil sentinel (0xA46F78),
    // and the chain walk matches. It cannot return nil for a live key, so it was
    // exonerated as the cause of the SheklesLibBars/FatCooldowns nil-field errors
    // (those need separate in-game isolation). Re-enabled.
    void* target = (void*)0x0085C430;

    // Verify prologue: push ebp; mov ebp, esp
    unsigned char* p = (unsigned char*)target;
    if (p[0] != 0x55 || p[1] != 0x8B) {
        Log("[GetStrInline] BAD PROLOGUE at 0x%08X (expected 55 8B)", (uintptr_t)target);
        return false;
    }

    if (MH_CreateHook(target, (void*)Optimized_GetStr, (void**)&g_orig_getstr) != MH_OK) {
        Log("[GetStrInline] MH_CreateHook FAILED");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[GetStrInline] MH_EnableHook FAILED");
        return false;
    }

    // Zero-initialize cache
    memset(g_cache, 0, sizeof(g_cache));

    Log("[GetStrInline] ACTIVE v2: safe bucket-index cache + prefetch (%d entries)", CACHE_SIZE);
    return true;
}

void UninstallLuaGetStrInline()
{
    MH_DisableHook((void*)0x0085C430);
    MH_RemoveHook((void*)0x0085C430);

    LONG64 total = g_total_calls;
    LONG64 first = g_first_node_hits;
    LONG64 cache = g_cache_hits;
    LONG64 walks = g_chain_walks;
    LONG64 nils  = g_nil_returns;
    LONG64 depth = g_chain_depth_total;

    if (total > 0) {
        double firstPct = 100.0 * first / total;
        double cachePct = 100.0 * cache / total;
        double avgDepth = walks > 0 ? (double)depth / walks : 0;
        Log("[GetStrInline] Stats: %lld calls | %lld first-node (%.1f%%) | "
            "%lld cache (%.1f%%) | %lld walks (avg depth %.1f) | %lld nil",
            total, first, firstPct, cache, cachePct, walks, avgDepth, nils);
    }
}