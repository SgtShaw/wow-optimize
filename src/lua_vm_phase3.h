#pragma once
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
