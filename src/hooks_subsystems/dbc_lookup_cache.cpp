// ============================================================================
// Module: dbc_lookup_cache.cpp
// Description: Supporting utility functions for `dbc_lookup_cache.cpp`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <cstring>
#include "MinHook.h"
#include "version.h"
#include "dbc_lookup_cache.h"

extern "C" void Log(const char* fmt, ...);

static constexpr int CACHE_SIZE = 4096;
static constexpr int CACHE_MASK = CACHE_SIZE - 1;

struct DbcRowEntry {
    uintptr_t storePtr;   // DBCStore* — identifies which DBC file
    uint32_t  recordId;
    void*     rowPtr;     // pointer to the raw DBC row data
    bool      valid;
};

static DbcRowEntry g_cache[CACHE_SIZE];
static uint64_t   g_hits = 0;
static uint64_t   g_misses = 0;

// __thiscall: this=ECX, recordId=[esp+4], outBuf=[esp+8]
// Hook uses __fastcall (same register convention) to bypass MSVC restriction
typedef bool (__thiscall *orig_dbc_getrow_t)(void* store, int recordId, void* outBuf);
static orig_dbc_getrow_t g_orig = nullptr;

static bool __fastcall Hooked_DbcGetRow(void* store, void* /* edx */, int recordId, void* outBuf)
{
    uintptr_t storeKey = (uintptr_t)store;
    uint32_t idx = ((uint32_t)(storeKey >> 2) ^ recordId) & CACHE_MASK;
    DbcRowEntry* e = &g_cache[idx];

    if (e->valid && e->storePtr == storeKey && e->recordId == (uint32_t)recordId) {
        g_hits++;
        if (outBuf && e->rowPtr) {
            memcpy(outBuf, e->rowPtr, 0x2A8);
        }
        return e->rowPtr != nullptr;
    }

    g_misses++;
    bool result = g_orig(store, recordId, outBuf);

    if (result && outBuf && store) {
        // The row pointer is at store->rows[recordId - store->minId]
        // We can't easily get it from the output, so store the storePtr+recordId
        // and use the DBCStore internals to resolve the row pointer on hit.
        // Actually: the original function already did the lookup. The row pointer
        // is in the DBCStore's internal array. We can compute it the same way.
        uintptr_t storeBase = (uintptr_t)store;
        uint32_t minId = *(uint32_t*)(storeBase + 0x10);
        uint32_t maxId = *(uint32_t*)(storeBase + 0x0C);
        uintptr_t rowsBase = *(uintptr_t*)(storeBase + 0x20);

        if (recordId >= (int)minId && recordId <= (int)maxId && rowsBase > 0x10000) {
            void* rowPtr = *(void**)(rowsBase + (recordId - minId) * 4);
            if (rowPtr && (uintptr_t)rowPtr > 0x10000 && (uintptr_t)rowPtr < 0xFFE00000) {
                e->storePtr = storeKey;
                e->recordId = (uint32_t)recordId;
                e->rowPtr = rowPtr;
                e->valid = true;
            }
        }
    }

    return result;
}

bool InstallDbcLookupCache()
{
    // DISABLED: net-negative + a correctness hole. The original sub_4CFD20 is
    // already O(1): a bounds check + array index + qmemcpy(0x2A8). This cache
    // does the SAME 0x2A8 memcpy on a hit plus hash/compare overhead, so it is
    // slower, not faster. Worse, the original has a branch
    //   if (byte_C5DEA0) sub_4CFBB0(row, 680, out); else qmemcpy(out, row, 0x2A8);
    // and the cache hit path always does the plain qmemcpy, returning wrong
    // (untransformed) data whenever byte_C5DEA0 is set. Let the original run.
    (void)&Hooked_DbcGetRow;
    Log("[DbcLookupCache] DISABLED (original GetRow already O(1); cache was net-negative + skipped sub_4CFBB0 transform)");
    return false;

#if 0
    memset(g_cache, 0, sizeof(g_cache));
    g_hits = 0;
    g_misses = 0;

    void* target = reinterpret_cast<void*>(0x004CFD20);

    unsigned char* p = (unsigned char*)target;
    if (p[0] != 0x55 || p[1] != 0x8B || p[2] != 0xEC) {
        Log("[DbcLookupCache] BAD PROLOGUE at 0x%08X (expected 55 8B EC)", (uintptr_t)target);
        return false;
    }

    if (WineSafe_CreateHook(target, (void*)Hooked_DbcGetRow, (void**)&g_orig) != MH_OK) {
        Log("[DbcLookupCache] MH_CreateHook FAILED");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[DbcLookupCache] MH_EnableHook FAILED");
        MH_RemoveHook(target);
        return false;
    }

    Log("[DbcLookupCache] Installed: %d-slot cache at 0x4CFD20 (250+ callers, immutable DBC rows)", CACHE_SIZE);
    return true;
#endif
}

void UninstallDbcLookupCache()
{
    void* target = reinterpret_cast<void*>(0x004CFD20);
    MH_DisableHook(target);
    MH_RemoveHook(target);

    uint64_t total = g_hits + g_misses;
    if (total > 0) {
        Log("[DbcLookupCache] Stats: %llu calls, %llu hits, %llu misses (%.1f%% hit rate)",
            total, g_hits, g_misses, 100.0 * g_hits / total);
    }
}
