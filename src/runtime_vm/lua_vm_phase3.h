#pragma once

// ============================================================================
// Module: lua_vm_phase3.h
// Description: Accelerates Lua runtime calls in `lua_vm_phase3.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================


/**
 * @domain: Lua VM C-API Fast Path
 * @architecture: Optimizes C-API transitions for Lua state queries in `lua_vm_phase3.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Always maintain the Lua stack index alignment to prevent top index desynchronization.
 */



/**
 * @domain: Lua Virtual Machine Engine
 * @architecture: Fastpath detour hooks mapping hottest Lua VM interpreter instructions directly to C-level structures.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Incorrect Lua stack balance adjustments or thread-local storage collisions will result in UI freeze and transition crashes.
 */


#ifndef LUA_VM_PHASE3_H
#define LUA_VM_PHASE3_H

#include <windows.h>
#include <cstdint>
#include <unordered_map>

typedef struct lua_State lua_State;

// Forward declaration for the fast-path handler
typedef int (*LuaFastPathHandler)(lua_State* L);

namespace LuaVMPhase3 {

// Initialize the Phase 3 hooks
bool Init();

// Shutdown and cleanup
void Shutdown();

// Register a fast-path handler for a specific Lua function address
void RegisterFastPath(void* lua_func_addr, LuaFastPathHandler handler);

// The main hook that replaces luaD_call
int __cdecl Hooked_luaD_call(lua_State* L, int nResults);

struct Stats {
    long bypassHits;
    long bypassMisses;
    bool active;
};

Stats GetStats();

} // namespace LuaVMPhase3

#endif
