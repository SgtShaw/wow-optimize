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
        if (top < 0x10000 || top > 0xFFE00000) return nullptr;
        uintptr_t tv = top - 16;
        if (tv < 0x10000 || tv > 0xFFE00000) return nullptr;
        uint32_t tt = *(uint32_t*)(tv + 8);
        if (tt != 4) return nullptr;
        uintptr_t ts = *(uintptr_t*)tv;
        if (ts < 0x10000 || ts > 0xFFE00000) return nullptr;
        size_t len = *(size_t*)(ts + 16);
        if (len > 4000) len = 4000;
        return (const char*)(ts + 20);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

typedef int (__cdecl *lua_getstack_fn)(uintptr_t L, int level, void* ar);
typedef int (__cdecl *lua_getinfo_fn)(uintptr_t L, const char* what, void* ar);

static void LogLuaTraceback(uintptr_t L) {
    if (!L) return;
    
    auto getstack = (lua_getstack_fn)0x0084FE40;
    auto getinfo = (lua_getinfo_fn)0x00850A90;

    char ar[100];
    memset(ar, 0, sizeof(ar));
    int level = 0;
    
    LogEx(LOG_LEVEL_ERROR, "LUA", "  Traceback:");
    while (getstack(L, level, ar) == 1) {
        getinfo(L, "nSl", ar);
        
        const char* name = *(const char**)(ar + 4);
        const char* namewhat = *(const char**)(ar + 8);
        const char* what = *(const char**)(ar + 12);
        const char* source = *(const char**)(ar + 16);
        int currentline = *(int*)(ar + 20);
        const char* short_src = (const char*)(ar + 36);
        
        if (!name) name = "?";
        if (!short_src) short_src = "?";
        if (!what) what = "?";
        
        LogEx(LOG_LEVEL_ERROR, "LUA", "    [%d] %s:%d in function '%s' (%s)",
            level, short_src, currentline, name, what);
            
        level++;
        if (level > 20) {
            LogEx(LOG_LEVEL_ERROR, "LUA", "    ... (truncated)");
            break;
        }
    }
}

static int __cdecl DiagLuaError(uintptr_t L) {
    LONG errNum = InterlockedIncrement(&g_errorCount);
    if (errNum > MAX_DIAG_ERRORS) return s_origLuaError(L);

    uintptr_t useL = L;
    if (L < 0x10000 || L > 0xFFE00000) {
        useL = *(uintptr_t*)0x00D3F78C;
        if (useL < 0x10000 || useL > 0xFFE00000) useL = 0;
    }

    bool isCaught = false;
    if (useL) {
        __try {
            // Offset 0x74 is L->errorJmp in Lua 5.1 32-bit structure.
            uintptr_t errorJmp = *(uintptr_t*)(useL + 0x74);
            if (errorJmp != 0) {
                isCaught = true;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    const char* errMsg = nullptr;
    if (useL) {
        errMsg = ReadLuaErrorString(useL);
    }
    if (!errMsg) errMsg = "<unable to read>";

    if (isCaught) {
        // Silently log caught errors as a single line to avoid log flooding
        LogEx(LOG_LEVEL_INFO, "LUA", "[LuaError] Caught exception: %s", errMsg);
        return s_origLuaError(L);
    }

    LogEx(LOG_LEVEL_ERROR, "LUA", "=== UNHANDLED LUA ERROR #%d ===", (int)errNum);
    LogEx(LOG_LEVEL_ERROR, "LUA", "  lua_State param: 0x%08X  global: 0x%08X", (unsigned)L, (unsigned)useL);
    LogEx(LOG_LEVEL_ERROR, "LUA", "  Message: %s", errMsg);

    if (useL) {
        LogLuaTraceback(useL);
    }

    LogEx(LOG_LEVEL_ERROR, "LUA", "  DLL optimization features status:");
    FeatureState features[64];
    int fcount = CrashDumper::GetFeatureStates(features, 64);
    for (int i = 0; i < fcount; i++) {
        LogEx(LOG_LEVEL_ERROR, "LUA", "    %-28s active=%d calls=%lld errors=%lld",
            features[i].name ? features[i].name : "(null)",
            features[i].active ? 1 : 0,
            features[i].callCount,
            features[i].errorCount);
    }

    LogEx(LOG_LEVEL_ERROR, "LUA", "  Last 32 hook calls:");
    extern void CrashDumper_DumpHookTrace(int count);
    CrashDumper_DumpHookTrace(32);

    LogEx(LOG_LEVEL_ERROR, "LUA", "=== END LUA ERROR #%d ===", (int)errNum);

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
