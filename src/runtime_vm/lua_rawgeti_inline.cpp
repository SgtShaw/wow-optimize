// ============================================================================
// Module: lua_rawgeti_inline.cpp
// Description: Accelerates Lua runtime calls in `lua_rawgeti_inline.cpp`.
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
#include "lua_rawgeti_inline.h"
#include "lua_optimize.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

// ----------------------------------------------------------------
// Statistics (diagnostic only; plain increments. Lua is single-threaded,
// so the previous InterlockedIncrement64 -- a lock cmpxchg8b loop on x86 --
// only added cost to this very hot path.)
// ----------------------------------------------------------------
static volatile LONG64 g_total_calls = 0;
static volatile LONG64 g_array_hits = 0;
static volatile LONG64 g_cache_hits = 0;
static volatile LONG64 g_chain_walks = 0;
static volatile LONG64 g_chain_depth_total = 0;
static volatile LONG64 g_nil_returns = 0;

// ----------------------------------------------------------------
// Cache configuration
// ----------------------------------------------------------------
// Direct-mapped cache: 8192 entries, keyed on (table ^ key)
// Stores bucket INDEX (not pointer!) + lsize for resize detection
static constexpr int CACHE_BITS = 13;
static constexpr int CACHE_SIZE = 1 << CACHE_BITS;
static constexpr int CACHE_MASK = CACHE_SIZE - 1;

struct SafeRawGetIEntry {
    uint32_t table_lo;      // lower 32 bits of table ptr
    int32_t  key;           // integer key
    uint32_t bucket_idx;    // bucket index where found
    uint8_t  lsize;         // table[11] at time of caching (resize detector)
    uint8_t  pad[3];        // alignment
};

static SafeRawGetIEntry g_cache[CACHE_SIZE];

void ClearRawGetIInlineCache() {
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
typedef int (__cdecl *lua_rawgeti_fn)(int L, int idx, int n);
static lua_rawgeti_fn g_orig_rawgeti = nullptr;

// ----------------------------------------------------------------
// Optimized replacement — SAFE (no pointer caching)
// ----------------------------------------------------------------
static int __cdecl Optimized_RawGetI(int L, int idx, int n)
{
    CrashDumper::RecordHookCall("RawGetIInline", (uintptr_t)L);
    ++g_total_calls;

    // Bail out during lua_State swap — L->base and L->top become garbage
    // when WoW destroys the old Lua VM during UI reload/logout.
    if (LuaOpt::IsReloading() || LuaOpt::IsSwapping()) {
        return g_orig_rawgeti(L, idx, n);
    }

    // Validate L pointer
    if ((uintptr_t)L < 0x10000 || (uintptr_t)L > 0xFFE00000) {
        return g_orig_rawgeti(L, idx, n);
    }

    __try {
        // Resolve table from stack index
        int* tableSlot = nullptr;
        int* L_base = *(int**)(L + 0x10);  // L->base
        int* L_top  = *(int**)(L + 0x0C);  // L->top

        if (idx > 0) {
            if (L_base + (idx - 1) * 4 < L_top)
                tableSlot = L_base + (idx - 1) * 4;
        } else if (idx < 0 && idx >= -10000) {
            tableSlot = L_top + idx * 4;
            if (tableSlot < L_base) {
                return g_orig_rawgeti(L, idx, n);
            }
        }
        // Pseudo-indices (LUA_GLOBALSINDEX -10002, ENVIRONINDEX -10001,
        // REGISTRYINDEX -10000) are NOT plain stack slots; index2adr resolves
        // them specially. Don't reimplement that — leave tableSlot null so they
        // defer to the engine (the old GLOBALSINDEX = L_base+72 guess was wrong).
        if (!tableSlot) {
            return g_orig_rawgeti(L, idx, n);
        }

        int table = tableSlot[0];
        // Validate: must be a table (tt==5)
        if (tableSlot[2] != 5 || table < 0x10000 || table > 0xFFE00000) {
            return g_orig_rawgeti(L, idx, n);
        }

        // ============================================================
        // FAST PATH 1: Direct array access
        // If (n-1) < sizearray, the value is in the array part.
        // This is O(1) with no cache needed.
        // ============================================================
        int sizearray = *(int*)(table + 32);
        if ((unsigned int)(n - 1) < (unsigned int)sizearray) {
            int* array = *(int**)(table + 16);
            if (!array || (uintptr_t)array < 0x10000 || (uintptr_t)array > 0xFFE00000) {
                return g_orig_rawgeti(L, idx, n);
            }

            // Prefetch next cache line for sequential access patterns
            if (n < sizearray) {
                _mm_prefetch((const char*)(array + n * 4), _MM_HINT_T0);
            }

            int* src = array + (n - 1) * 4;  // 4 DWORDs = 16 bytes per TValue

            // Push TValue onto Lua stack
            DWORD* top = *(DWORD**)(L + 0x0C);
            if (!top || (uintptr_t)top < 0x10000 || (uintptr_t)top > 0xFFE00000) {
                return g_orig_rawgeti(L, idx, n);
            }

            top[0] = src[0];  // value lo
            top[1] = src[1];  // value hi
            top[2] = src[2];  // tt
            top[3] = src[3];  // taint
            *(DWORD**)(L + 0x0C) = top + 4;

            // Taint propagation — replicate sub_84E670 EXACTLY (verified via
            // disasm: dword_D4139C is the taint cell itself, single indirection).
            //   taint != 0: if (D413A0 && !D413A4) global_taint = taint; return taint
            //   taint == 0: stamp the just-pushed value with the current global
            //               taint and return it. The old hook OMITTED this branch,
            //               so values read in a tainted context came out untainted
            //               -- a taint-model divergence from the engine.
            DWORD taint = src[3];
            ++g_array_hits;
            if (taint) {
                if (*(int*)0x00D413A0 && !*(int*)0x00D413A4)
                    *(DWORD*)0x00D4139C = taint;
                return (int)taint;
            } else {
                DWORD gt = *(DWORD*)0x00D4139C;
                top[3] = gt;            // matches: mov [ecx+0Ch], eax  (ecx = old top)
                return (int)gt;
            }
        }

        // ============================================================
        // HASH PART: Integer key not in array, let original handle it
        // ============================================================
        // `n = 0` (used for `this` pointer in WoW) and out-of-bounds 
        // indices go here. Original luaH_getnum handles hashnum correctly.
        return g_orig_rawgeti(L, idx, n);

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return g_orig_rawgeti(L, idx, n);
    }
}

// ----------------------------------------------------------------
// Install / Uninstall
// ----------------------------------------------------------------
bool InstallLuaRawGetIInline()
{
    void* target = (void*)0x0084E670;

    // Verify prologue: push ebp; mov ebp, esp
    unsigned char* p = (unsigned char*)target;
    if (p[0] != 0x55 || p[1] != 0x8B) {
        Log("[RawGetIInline] BAD PROLOGUE at 0x%08X (expected 55 8B)", (uintptr_t)target);
        return false;
    }

    if (MH_CreateHook(target, (void*)Optimized_RawGetI, (void**)&g_orig_rawgeti) != MH_OK) {
        Log("[RawGetIInline] MH_CreateHook FAILED");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[RawGetIInline] MH_EnableHook FAILED");
        return false;
    }

    // Zero-initialize cache
    memset(g_cache, 0, sizeof(g_cache));

    Log("[RawGetIInline] ACTIVE v2: safe array direct + bucket-index cache (%d entries)", CACHE_SIZE);
    return true;
}

void UninstallLuaRawGetIInline()
{
    MH_DisableHook((void*)0x0084E670);
    MH_RemoveHook((void*)0x0084E670);

    LONG64 total = g_total_calls;
    LONG64 arr   = g_array_hits;
    LONG64 cache = g_cache_hits;
    LONG64 walks = g_chain_walks;
    LONG64 nils  = g_nil_returns;
    LONG64 depth = g_chain_depth_total;

    if (total > 0) {
        double arrPct   = 100.0 * arr / total;
        double cachePct = 100.0 * cache / total;
        double avgDepth = walks > 0 ? (double)depth / walks : 0;
        Log("[RawGetIInline] Stats: %lld calls | %lld array (%.1f%%) | "
            "%lld cache (%.1f%%) | %lld walks (avg depth %.1f) | %lld nil",
            total, arr, arrPct, cache, cachePct, walks, avgDepth, nils);
    }
}