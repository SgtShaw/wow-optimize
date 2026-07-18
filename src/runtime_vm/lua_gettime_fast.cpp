// ============================================================================
// Module: lua_gettime_fast.cpp
// Description: Accelerates Lua GetTime API calls via frame caching.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================

#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"
#include "lua_gettime_fast.h"

extern "C" void Log(const char* fmt, ...);

// Address of the Lua GetTime CFunction
#define ADDR_LUA_GETTIME 0x006081F0

// lua_pushnumber helper address
#define ADDR_LUA_PUSHNUMBER 0x0084E2A0

typedef int (__cdecl *orig_gettime_fn)(int L);
static orig_gettime_fn orig_LuaGetTime = nullptr;

static volatile DWORD g_lastGetTimeFrameTick = 0;
static volatile double g_cachedGetTimeValue = 0.0;
static volatile DWORD g_cachedValueFrameTick = 0xFFFFFFFF;

static int __cdecl Hooked_LuaGetTime(int L) {
    if (L < 0x10000 || L > 0xFFE00000) {
        return orig_LuaGetTime(L);
    }

    DWORD currentFrameTick = g_lastGetTimeFrameTick;

    // Check if we have a valid cache hit for this frame
    if (g_cachedValueFrameTick == currentFrameTick) {
        typedef void (__cdecl *pushnumber_fn)(int, double);
        __try {
            ((pushnumber_fn)ADDR_LUA_PUSHNUMBER)(L, g_cachedGetTimeValue);
            return 1;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    // Cache miss - call the original GetTime
    int res = orig_LuaGetTime(L);

    __try {
        // Read the top of the Lua stack to retrieve the pushed number
        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        if (top >= 0x10000 && top <= 0xFFE00000) {
            uintptr_t val_tv = top - 16;
            if (*(int*)(val_tv + 8) == 3) { // LUA_TNUMBER is 3
                g_cachedGetTimeValue = *(double*)val_tv;
                g_cachedValueFrameTick = currentFrameTick;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    return res;
}

void LuaGetTimeFast_NewFrame() {
    // Advance the frame tick so the cache is invalidated for the new frame
    g_lastGetTimeFrameTick++;
}

bool InstallLuaGetTimeFast() {
    void* target = (void*)ADDR_LUA_GETTIME;
    if (MH_CreateHook(target, (void*)Hooked_LuaGetTime, (void**)&orig_LuaGetTime) != MH_OK) {
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        return false;
    }
    Log("[GetTimeFast] ACTIVE — lua_GetTime cached at 0x%08X", ADDR_LUA_GETTIME);
    CrashDumper::RegisterFeature("GetTimeFast");
    CrashDumper::FeatureSetActive("GetTimeFast", true);
    return true;
}
