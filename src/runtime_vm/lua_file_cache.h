#pragma once
// ============================================================================
// Module: lua_file_cache.h
// Description: Lua bytecode file cache API
// ============================================================================

#include <cstdint>

// Lookup cached bytecode for a given path. Returns nullptr on miss.
const uint8_t* LuaFileCache_Lookup(const char* path, uint32_t* outSize);

// Store compiled bytecode in cache. Returns false on failure.
bool LuaFileCache_Store(const char* path, const uint8_t* bytecode, uint32_t size);

// Clear entire cache (call on lua_State swap / UI reload).
void LuaFileCache_Clear();

// Install/initialize the cache system.
bool InstallLuaFileCache();

// Shutdown and free all cached data.
void ShutdownLuaFileCache();