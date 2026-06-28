#pragma once

// ============================================================================
// Module: lua_getstr_inline.h
// Description: Accelerates Lua runtime calls in `lua_getstr_inline.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================


/**
 * @domain: Lua VM C-API Fast Path
 * @architecture: Optimizes C-API transitions for Lua state queries in `lua_getstr_inline.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Always maintain the Lua stack index alignment to prevent top index desynchronization.
 */



/**
 * @domain: Lua Virtual Machine Engine
 * @architecture: Fastpath detour hooks mapping hottest Lua VM interpreter instructions directly to C-level structures.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Incorrect Lua stack balance adjustments or thread-local storage collisions will result in UI freeze and transition crashes.
 */



bool InstallLuaGetStrInline();
void UninstallLuaGetStrInline();
void InvalidateLuaGetStrInlineCache();  // zeroes the entire 16384-entry bucket cache
