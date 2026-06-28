#pragma once

// ============================================================================
// Module: lua_internals.h
// Description: Accelerates Lua runtime calls in `lua_internals.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================


/**
 * @domain: Lua VM C-API Fast Path
 * @architecture: Optimizes C-API transitions for Lua state queries in `lua_internals.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Always maintain the Lua stack index alignment to prevent top index desynchronization.
 */



/**
 * @domain: Lua Virtual Machine Engine
 * @architecture: Fastpath detour hooks mapping hottest Lua VM interpreter instructions directly to C-level structures.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Incorrect Lua stack balance adjustments or thread-local storage collisions will result in UI freeze and transition crashes.
 */


#ifndef LUA_INTERNALS_H
#define LUA_INTERNALS_H

#include <windows.h>

namespace LuaInternals {

bool Init();
void Shutdown();
void OnGCStep();
void InvalidateCache();

struct Stats {
    long concatFastHits;
    long concatFallbacks;
    bool active;
};

Stats GetStats();

} // namespace LuaInternals

#endif