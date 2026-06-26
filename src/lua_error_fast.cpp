#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);
extern void LogFlushImmediate();
extern void CrashDumper_DumpHookTrace(int count);

typedef int(__cdecl *error_fn)(uintptr_t L);
static error_fn orig = nullptr;
static volatile LONG g_errorCount = 0;
static volatile LONG g_hits = 0;

#define MAX_LOG_ERRORS 100

static const char* ReadErrorString(uintptr_t L) {
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

static int __cdecl hook(uintptr_t L) {
    if (L > 0x10000 && L < 0xBFFF0000) {
        __try {
            uintptr_t top = *(uintptr_t*)(L + 0x0C);
            if (top > 0x10000 && top < 0xBFFF0000) {
                uintptr_t tv = top - 16;
                if (tv > 0x10000 && tv < 0xBFFF0000) {
                    uint32_t tt = *(uint32_t*)(tv + 8);
                    if (tt == 4) {
                        g_hits++;
                    }
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    LONG errNum = InterlockedIncrement(&g_errorCount);
    if (errNum <= MAX_LOG_ERRORS) {
        const char* msg = ReadErrorString(L);
        Log("");
        Log("=== LUA ERROR #%d ===", (int)errNum);
        Log("  lua_State: 0x%08X", (unsigned)L);
        if (msg && msg[0]) {
            Log("  Message: %s", msg);
        } else {
            Log("  Message: <unable to read>");
        }
        Log("  Last 32 hook calls:");
        CrashDumper_DumpHookTrace(32);
        Log("=== END LUA ERROR #%d ===", (int)errNum);
        Log("");
        if (errNum <= 10) LogFlushImmediate();
    }

    return orig(L);
}

bool InstallLuaErrorFast() {
    // IDA-verified: 0x84EF30 = sub_84EF30 = lua_error, __cdecl, __noreturn.
    // Calls sub_850830 (luaD_throw) directly with lua_State* arg. 12 bytes.
    // Previous target 0x84F610 was sub_84F610(size_t) = luaL_addvalue, NOT lua_error.
    void* t = (void*)0x0084EF30;
    if (*(unsigned char*)t != 0x55 || *((unsigned char*)t + 1) != 0x8B) {
        Log("[ErrorFast] BAD PROLOGUE at 0x%08X (expected 55 8B), skipping", (uintptr_t)t);
        return false;
    }
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    MH_EnableHook(t);
    Log("[ErrorFast] ACTIVE at 0x84EF30 (lua_error): logging first %d Lua errors + hook trace", MAX_LOG_ERRORS);
    CrashDumper::RegisterFeature("ErrorFast");
    CrashDumper::FeatureSetActive("ErrorFast", true);
    return true;
}
