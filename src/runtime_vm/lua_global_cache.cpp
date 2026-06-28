// ============================================================================
// Module: lua_global_cache.cpp
// Description: Accelerates Lua runtime calls in `lua_global_cache.cpp`. Caches structures to bypass parser overhead.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================

#include "lua_global_cache.h"
#include <windows.h>

extern "C" void Log(const char* fmt, ...);

bool InstallLuaGlobalCache() {
    // Disabled - function signature mismatch
    Log("[LuaGlobalCache] Disabled - prototype mismatch (3 args, not 2)");
    return false;
}

void UninstallLuaGlobalCache() {}
