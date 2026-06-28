#pragma once

// ============================================================================
// Module: lua_bytecode_cache.h
// Description: Accelerates Lua runtime calls in `lua_bytecode_cache.h`. Caches structures to bypass parser overhead.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================


/**
 * @domain: Lua VM Caching Subsystem
 * @architecture: Caches Lua runtime execution structures in `lua_bytecode_cache.h` to avoid redundant operations.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Stale table pointers must be invalidated upon GC cycles or rehashes to prevent stale memory access.
 */



/**
 * @domain: Lua Virtual Machine Engine
 * @architecture: Fastpath detour hooks mapping hottest Lua VM interpreter instructions directly to C-level structures.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Incorrect Lua stack balance adjustments or thread-local storage collisions will result in UI freeze and transition crashes.
 */



// Caches the precompiled Lua chunk produced by luaL_loadbuffer keyed by
// FNV-1a(source). On hit, replays bytecode to skip the parser entirely.
// Cache cleared on lua_State swap (bytecode is VM-bound).

#include <windows.h>
#include <cstdint>

namespace LuaBytecodeCache {

bool Init();
void Shutdown();
void OnLuaStateSwap();

struct Stats {
    bool     active;
    uint32_t entries;
    uint64_t hits;
    uint64_t misses;
    uint64_t bypasses;
    uint64_t loadFailures;
    uint64_t bytesCached;
};
void GetStats(Stats* out);

} // namespace LuaBytecodeCache
