#pragma once

// ============================================================================
// Module: lua_gettable_safety.h
// Description: Accelerates Lua runtime calls in `lua_gettable_safety.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================


/**
 * @domain: Lua VM C-API Fast Path
 * @architecture: Optimizes C-API transitions for Lua state queries in `lua_gettable_safety.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Always maintain the Lua stack index alignment to prevent top index desynchronization.
 */



/**
 * @domain: Lua Virtual Machine Engine
 * @architecture: Fastpath detour hooks mapping hottest Lua VM interpreter instructions directly to C-level structures.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Incorrect Lua stack balance adjustments or thread-local storage collisions will result in UI freeze and transition crashes.
 */


// lua_gettable_safety.h - Crash fix for sub_85BC10 TValue corruption

bool InstallLuaGetTableSafety();
void UninstallLuaGetTableSafety();
LONG64 GetTableSafety_GetBlockedCount();
LONG64 GetTableSafety_GetTotalCount();