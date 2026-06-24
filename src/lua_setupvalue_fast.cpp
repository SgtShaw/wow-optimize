#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

typedef const char*(__cdecl *setupvalue_fn)(uintptr_t L, int funcindex, int n);
static setupvalue_fn orig = nullptr;
static volatile long g_hits = 0, g_misses = 0;

static const char* __cdecl hook(uintptr_t L, int funcindex, int n) {
    if (L > 0x10000 && L < 0xBFFF0000 && n > 0 && n < 256) {
        __try {
            uintptr_t top = *(uintptr_t*)(L + 0x0C);
            if (top > 0x10000 && top < 0xBFFF0000) {
                uintptr_t tv = top - 16;
                if (tv > 0x10000 && tv < 0xBFFF0000) {
                    uint32_t tt = *(uint32_t*)(tv + 8);
                    if (tt == 7) {
                        uint64_t val = *(uint64_t*)(tv);
                        uint32_t taint = *(uint32_t*)(tv + 12);
                        *(uint64_t*)(top) = val;
                        *(uint32_t*)(top + 8) = tt;
                        *(uint32_t*)(top + 12) = taint;
                        *(uintptr_t*)(L + 0x0C) = top + 16;
                        g_hits++;
                        return (const char*)(tv + 24);
                    }
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    g_misses++;
    return orig(L, funcindex, n);
}

bool InstallLuaSetUpvalFast() {
    void* t = (void*)0x0084F2F0;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    MH_EnableHook(t);
    Log("[SetUpval] ACTIVE — lua_setupvalue inline at 0x84F2F0");
    CrashDumper::RegisterFeature("SetUpval");
    CrashDumper::FeatureSetActive("SetUpval", true);
    return true;
}
