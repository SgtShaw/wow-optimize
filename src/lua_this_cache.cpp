// Lua "this" cache - disabled. __usercall (ESI param) incompatible with MinHook.
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
