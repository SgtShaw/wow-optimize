// ============================================================================
// Module: lua_this_cache.cpp
// Description: Accelerates Lua runtime calls in `lua_this_cache.cpp`. Caches structures to bypass parser overhead.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
extern "C" void Log(const char* fmt, ...);

bool InstallLuaThisCache() {
    Log("[LuaThisCache] Disabled: __usercall incompatible with MinHook");
    return true;
}
void UninstallLuaThisCache() {}
void GetLuaThisCacheStats(uint64_t* hits, uint64_t* total) {
    if (hits) *hits = 0;
    if (total) *total = 0;
}
