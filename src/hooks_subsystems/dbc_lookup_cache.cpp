// ============================================================================
// Module: dbc_lookup_cache.cpp
// Description: Fast O(1) transformed row cache for DBC database queries.
// Safety & Threading: Thread-safe, executes on main/render threads.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <atomic>
#include "MinHook.h"
#include "version.h"
#include "dbc_lookup_cache.h"

extern "C" void Log(const char* fmt, ...);

static constexpr int CACHE_SIZE = 4096;
static constexpr int CACHE_MASK = CACHE_SIZE - 1;

struct DbcRowEntry {
    uintptr_t storePtr;   // DBCStore* — identifies which DBC file
    uint32_t  recordId;
    uint8_t   data[0x2A8]; // Raw or transformed DBC record data
    bool      valid;
    std::atomic<bool> lock; // Spinlock to prevent concurrent read/write races
};

static DbcRowEntry g_cache[CACHE_SIZE];
static uint64_t   g_hits = 0;
static uint64_t   g_misses = 0;

typedef bool (__thiscall *orig_dbc_getrow_t)(void* store, int recordId, void* outBuf);
static orig_dbc_getrow_t g_orig = nullptr;

static bool __fastcall Hooked_DbcGetRow(void* store, void* /* edx */, int recordId, void* outBuf)
{
#if TEST_DISABLE_DBC_LOOKUP_CACHE
    return g_orig(store, recordId, outBuf);
#else
    uintptr_t storeKey = (uintptr_t)store;
    uint32_t idx = ((uint32_t)(storeKey >> 2) ^ recordId) & CACHE_MASK;
    DbcRowEntry* e = &g_cache[idx];

    // Attempt to read under spinlock
    bool hit = false;
    bool expected = false;
    while (!e->lock.compare_exchange_weak(expected, true, std::memory_order_acquire, std::memory_order_relaxed)) {
        expected = false;
        YieldProcessor();
    }

    if (e->valid && e->storePtr == storeKey && e->recordId == (uint32_t)recordId) {
        g_hits++;
        if (outBuf) {
            memcpy(outBuf, e->data, 0x2A8);
        }
        hit = true;
    }

    e->lock.store(false, std::memory_order_release);

    if (hit) {
        return true;
    }

    g_misses++;
    bool result = g_orig(store, recordId, outBuf);

    if (result && outBuf && store) {
        // Attempt to write under spinlock
        expected = false;
        while (!e->lock.compare_exchange_weak(expected, true, std::memory_order_acquire, std::memory_order_relaxed)) {
            expected = false;
            YieldProcessor();
        }

        e->storePtr = storeKey;
        e->recordId = (uint32_t)recordId;
        memcpy(e->data, outBuf, 0x2A8);
        e->valid = true;

        e->lock.store(false, std::memory_order_release);
    }

    return result;
#endif
}

bool InstallDbcLookupCache()
{
    for (int i = 0; i < CACHE_SIZE; i++) {
        g_cache[i].storePtr = 0;
        g_cache[i].recordId = 0;
        g_cache[i].valid = false;
        g_cache[i].lock.store(false, std::memory_order_relaxed);
    }
    g_hits = 0;
    g_misses = 0;

    void* target = reinterpret_cast<void*>(0x004CFD20);

    unsigned char prologue[3];
    __try {
        memcpy(prologue, target, 3);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("[DbcLookupCache] Target 0x004CFD20 not readable.");
        return true;
    }

    if (prologue[0] != 0x55 || prologue[1] != 0x8B || prologue[2] != 0xEC) {
        Log("[DbcLookupCache] BAD PROLOGUE at 0x%08X (expected 55 8B EC)", (uintptr_t)target);
        return true;
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

    Log("[DbcLookupCache] Installed: %d-slot transformed data cache at 0x4CFD20", CACHE_SIZE);
    return true;
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
