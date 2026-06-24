#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

typedef int(__cdecl *getinfo_fn)(uintptr_t L, const char* what, uintptr_t ar);
static getinfo_fn orig = nullptr;
static volatile long g_hits = 0, g_misses = 0;

static int __cdecl hook(uintptr_t L, const char* what, uintptr_t ar) {
    if (L > 0x10000 && L < 0xBFFF0000 && what > (const char*)0x10000 && what < (const char*)0xBFFF0000) {
        __try {
            if (what[0] == 'n' && what[1] == '\0') {
                if (ar > 0x10000 && ar < 0xBFFF0000) {
                    *(uint32_t*)(ar + 0x08) = 0;
                    *(uint32_t*)(ar + 0x0C) = 0;
                }
                g_hits++;
                return 1;
            }
            if (what[0] == 'S' && what[1] == '\0') {
                if (ar > 0x10000 && ar < 0xBFFF0000) {
                    *(uint32_t*)(ar + 0x04) = 0;
                    *(uint32_t*)(ar + 0x08) = 0;
                }
                g_hits++;
                return 1;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    g_misses++;
    return orig(L, what, ar);
}

bool InstallLuaGetInfoFast() {
    void* t = (void*)0x0084F3B0;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    MH_EnableHook(t);
    Log("[GetInfo] ACTIVE — lua_getinfo inline at 0x84F3B0");
    CrashDumper::RegisterFeature("GetInfo");
    CrashDumper::FeatureSetActive("GetInfo", true);
    return true;
}
