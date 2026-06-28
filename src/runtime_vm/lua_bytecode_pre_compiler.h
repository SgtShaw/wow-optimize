#pragma once

// ============================================================================
// Module: lua_bytecode_pre_compiler.h
// Description: Accelerates Lua runtime calls in `lua_bytecode_pre_compiler.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================


/**
 * @domain: Lua VM C-API Fast Path
 * @architecture: Optimizes C-API transitions for Lua state queries in `lua_bytecode_pre_compiler.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Always maintain the Lua stack index alignment to prevent top index desynchronization.
 */



/**
 * @domain: Lua Virtual Machine Engine
 * @architecture: Fastpath detour hooks mapping hottest Lua VM interpreter instructions directly to C-level structures.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Incorrect Lua stack balance adjustments or thread-local storage collisions will result in UI freeze and transition crashes.
 */



// Background worker that pre-reads Interface\AddOns\**\*.lua and
// WTF\Account\**\*.lua into the OS file cache so the main thread's
// luaL_loadbuffer at /reload time pays no disk latency.

#include <windows.h>
#include <cstdint>

namespace LuaBytecodePreCompiler {

bool Init();
void Shutdown();
void OnFrame();

struct Stats {
    bool     active;
    uint32_t filesScanned;
    uint32_t filesPreloaded;
    uint64_t bytesPreloaded;
    uint32_t workers;
    uint32_t queueDepth;
};
void GetStats(Stats* out);

} // namespace LuaBytecodePreCompiler
