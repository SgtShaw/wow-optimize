#pragma once

// ============================================================================
// Module: lua_gettable_cache.h
// Description: Accelerates Lua runtime calls in `lua_gettable_cache.h`. Caches structures to bypass parser overhead.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================


/**
 * @domain: Lua VM Caching Subsystem
 * @architecture: Caches Lua runtime execution structures in `lua_gettable_cache.h` to avoid redundant operations.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Stale table pointers must be invalidated upon GC cycles or rehashes to prevent stale memory access.
 */



/**
 * @domain: Lua Virtual Machine Engine
 * @architecture: Fastpath detour hooks mapping hottest Lua VM interpreter instructions directly to C-level structures.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Incorrect Lua stack balance adjustments or thread-local storage collisions will result in UI freeze and transition crashes.
 */



// luaV_gettable Fast Path Cache
// Hooks sub_857250 to cache table+key lookups
bool InstallLuaGetTableCache();
void ShutdownLuaGetTableCache();
