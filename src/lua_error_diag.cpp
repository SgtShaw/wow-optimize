#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);
extern void LogFlushImmediate();

// Target: lua_error at 0x84F610
// This intercepts EVERY Lua error before the engine's longjmp,
// logs the error message + last 32 hook trace entries, then
// passes through to the original function.

typedef int(__cdecl *lua_error_t)(uintptr_t L);
static lua_error_t s_origLuaError = nullptr;
static volatile LONG g_errorCount = 0;

// --- Helper: extract TString data from a TValue ---
// TValue layout: value at +0, tt at +8, taint at +12
// TString layout: hash at +0, next at +4, marked at +9, global at +12,
//                 len at +16, data at +20
static const char* ReadLuaErrorString(uintptr_t L) {
    __try {
        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        if (top < 0x10000 || top > 0xBFFF0000) return nullptr;

        // Error value is at top - 16 (one TValue before top)
        uintptr_t tv = top - 16;
        if (tv < 0x10000 || tv > 0xBFFF0000) return nullptr;

        uint32_t tt = *(uint32_t*)(tv + 8);
        if (tt != 4) return nullptr;  // Must be LUA_TSTRING

        uintptr_t ts = *(uintptr_t*)tv;
        if (ts < 0x10000 || ts > 0xBFFF0000) return nullptr;

        size_t len = *(size_t*)(ts + 16);
        if (len > 4000) len = 4000;  // Safety truncation

        return (const char*)(ts + 20);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// --- Hooked lua_error ---
static int __cdecl DiagLuaError(uintptr_t L) {
    LONG errNum = InterlockedIncrement(&g_errorCount);

    Log("");
    Log("=== LUA ERROR #%d ===", (int)errNum);
    Log("  lua_State: 0x%08X", (unsigned)L);

    // Extract and log the error message
    const char* errMsg = ReadLuaErrorString(L);
    if (errMsg && errMsg[0]) {
        Log("  Message: %s", errMsg);
    } else {
        Log("  Message: <unable to read>");
    }

    // Dump the last 32 hook trace entries
    Log("  Last 32 hook calls:");
    extern void CrashDumper_DumpHookTrace(int count);
    CrashDumper_DumpHookTrace(32);

    Log("=== END LUA ERROR #%d ===", (int)errNum);
    Log("");

    LogFlushImmediate();

    return s_origLuaError(L);
}

bool InstallLuaErrorDiag() {
    void* target = (void*)0x0084F610;
    if (MH_CreateHook(target, DiagLuaError, (void**)&s_origLuaError) != MH_OK) return false;
    MH_EnableHook(target);
    Log("[LuaErrorDiag] ACTIVE: logging every Lua error to this log file");
    CrashDumper::RegisterFeature("LuaErrorDiag");
    CrashDumper::FeatureSetActive("LuaErrorDiag", true);
    return true;
}
