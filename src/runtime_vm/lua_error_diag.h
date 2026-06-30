#pragma once

// ============================================================================
// Module: lua_error_diag.h
// Description: Accelerates Lua runtime calls in `lua_error_diag.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================


/**
 * @domain: Lua VM C-API Fast Path
 * @architecture: Optimizes C-API transitions for Lua state queries in `lua_error_diag.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Always maintain the Lua stack index alignment to prevent top index desynchronization.
 */



/**
 * @domain: Lua Virtual Machine Engine
 * @architecture: Fastpath detour hooks mapping hottest Lua VM interpreter instructions directly to C-level structures.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Incorrect Lua stack balance adjustments or thread-local storage collisions will result in UI freeze and transition crashes.
 */



// Installs a Lua error diagnostic hook.
// NOTE: 0x84F610 is verified against disassembly as sub_84F610(size_t Size) = luaL_addvalue,
// NOT lua_error. The correct lua_error address must be found via disassembly before
// re-enabling this hook. Gated by TEST_DISABLE_LUA_ERROR_DIAG in version.h.
bool InstallLuaErrorDiag();
