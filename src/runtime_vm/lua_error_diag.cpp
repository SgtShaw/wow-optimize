// ============================================================================
// Module: lua_error_diag.cpp
// Description: Accelerates Lua runtime calls in `lua_error_diag.cpp`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================

#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);
extern void LogFlushImmediate();

// lua_error: 0x84EF30 = sub_84EF30. Disassembly-verified: __cdecl __noreturn sub_84EF30(_DWORD *a1)
// { sub_850830(a1); } — directly calls luaD_throw. 12 bytes, prologue: 55 8B EC.
// The old address 0x84F610 was sub_84F610(size_t Size) = luaL_addvalue, NOT lua_error.
// Using 0x84F610 caused all error reads to show <unable to read> (arg is size_t, not L).

// Max errors to log before stopping (prevents recursive error flooding)
#define MAX_DIAG_ERRORS 50

typedef int(__cdecl *lua_error_t)(uintptr_t L);
static lua_error_t s_origLuaError = nullptr;
static volatile LONG g_errorCount = 0;

static const char* ReadLuaErrorString(uintptr_t L) {
    __try {
        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        if (top < 0x10000 || top > 0xBFFF0000) return nullptr;
        uintptr_t tv = top - 16;
        if (tv < 0x10000 || tv > 0xBFFF0000) return nullptr;
        uint32_t tt = *(uint32_t*)(tv + 8);
        if (tt != 4) return nullptr;
        uintptr_t ts = *(uintptr_t*)tv;
        if (ts < 0x10000 || ts > 0xBFFF0000) return nullptr;
        size_t len = *(size_t*)(ts + 16);
        if (len > 4000) len = 4000;
        return (const char*)(ts + 20);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static int __cdecl DiagLuaError(uintptr_t L) {
    LONG errNum = InterlockedIncrement(&g_errorCount);
    if (errNum > MAX_DIAG_ERRORS) return s_origLuaError(L);

    // If parameter L is bogus, try the global lua_State pointer
    uintptr_t useL = L;
    if (L < 0x10000 || L > 0xBFFF0000) {
        useL = *(uintptr_t*)0x00D3F78C;
        if (useL < 0x10000 || useL > 0xBFFF0000) useL = 0;
    }

    Log("");
    Log("=== LUA ERROR #%d ===", (int)errNum);
    Log("  lua_State param: 0x%08X  global: 0x%08X", (unsigned)L, (unsigned)useL);

    const char* errMsg = nullptr;
    if (useL) {
        errMsg = ReadLuaErrorString(useL);
    }
    if (errMsg && errMsg[0]) {
        Log("  Message: %s", errMsg);
    } else {
        Log("  Message: <unable to read>");
    }

    Log("  Last 32 hook calls:");
    extern void CrashDumper_DumpHookTrace(int count);
    CrashDumper_DumpHookTrace(32);

    Log("=== END LUA ERROR #%d ===", (int)errNum);
    Log("");

    if (errNum <= 10) LogFlushImmediate();  // flush first 10, then rely on crash dump
    return s_origLuaError(L);
}

bool InstallLuaErrorDiag() {
    // Disassembly-verified: 0x84EF30 = sub_84EF30 = lua_error. Prologue: 55 8B EC (push ebp; mov ebp,esp).
    // Calls sub_850830 (luaD_throw) directly. __cdecl(lua_State*), __noreturn.
    void* target = (void*)0x0084EF30;
    unsigned char* p = (unsigned char*)target;
    if (p[0] != 0x55 || p[1] != 0x8B) {
        Log("[LuaErrorDiag] BAD PROLOGUE at 0x%08X (expected 55 8B, got %02X %02X) — skipping",
            (uintptr_t)target, p[0], p[1]);
        return false;
    }
    if (MH_CreateHook(target, DiagLuaError, (void**)&s_origLuaError) != MH_OK) return false;
    MH_EnableHook(target);
    Log("[LuaErrorDiag] ACTIVE at 0x84EF30 (lua_error): logging first %d Lua errors", MAX_DIAG_ERRORS);
    CrashDumper::RegisterFeature("LuaErrorDiag");
    CrashDumper::FeatureSetActive("LuaErrorDiag", true);
    return true;
}
