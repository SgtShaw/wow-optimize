#pragma once

// ============================================================================
// Module: lua_optimize.h
// Description: Accelerates Lua runtime calls in `lua_optimize.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================


/**
 * @domain: Lua VM C-API Fast Path
 * @architecture: Optimizes C-API transitions for Lua state queries in `lua_optimize.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Always maintain the Lua stack index alignment to prevent top index desynchronization.
 */



/**
 * @domain: Lua Virtual Machine Engine
 * @architecture: Fastpath detour hooks mapping hottest Lua VM interpreter instructions directly to C-level structures.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Incorrect Lua stack balance adjustments or thread-local storage collisions will result in UI freeze and transition crashes.
 */


// Lua VM optimizer
#ifndef LUA_OPTIMIZE_H
#define LUA_OPTIMIZE_H

#include <windows.h>
#include <cstdint>

namespace LuaOpt {

// Call from worker thread. Validates addresses.
bool PrepareFromWorkerThread();

// Call from hooked_Sleep on main thread.
// First call: initializes GC + registers functions.
// Subsequent: incremental GC step.
void OnMainThreadSleep(DWORD mainThreadId, double frameMs = 0.0);

// Call on DLL unload.
void Shutdown();

// Combat mode (reduces GC during combat)
void SetCombatMode(bool inCombat);
// Returns true if client is in loading screen
bool IsLoadingMode();

struct Stats {
    bool   initialized;
    bool   gcOptimized;
    bool   allocatorReplaced;
    bool   functionsRegistered;
    double luaMemoryKB;
    int    gcStepsTotal;
    int    gcPause;
    int    gcStepMul;
};

Stats GetStats();

// Thread-safe swap/reload state queries for worker threads
bool IsReloading();
bool IsSwapping();
DWORD GetLastSwapTick();

// Restore original Lua allocator (safe to call during ExitProcess teardown)
void RestoreAllocator();

} // namespace LuaOpt

// Called on UI reload to clear luaH_getstr and lua_getfield caches
extern "C" void ClearLuaOptCaches();

#endif