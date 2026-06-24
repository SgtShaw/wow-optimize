#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

typedef int(__cdecl *error_fn)(uintptr_t L);
static error_fn orig = nullptr;
static volatile long g_hits = 0, g_misses = 0;

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
    g_misses++;
    return orig(L);
}

bool InstallLuaErrorFast() {
    void* t = (void*)0x0084F610;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    MH_EnableHook(t);
    Log("[ErrorFast] ACTIVE — lua_error inline at 0x84F610");
    CrashDumper::RegisterFeature("ErrorFast");
    CrashDumper::FeatureSetActive("ErrorFast", true);
    return true;
}
