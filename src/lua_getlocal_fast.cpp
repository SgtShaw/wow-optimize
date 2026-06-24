#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

typedef const char*(__cdecl *getlocal_fn)(uintptr_t L, uintptr_t ar, int n);
static getlocal_fn orig = nullptr;
static volatile long g_hits = 0, g_misses = 0;

static const char* __cdecl hook(uintptr_t L, uintptr_t ar, int n) {
    if (L > 0x10000 && L < 0xBFFF0000 && n > 0) {
        uintptr_t ci = *(uintptr_t*)(L + 0x18);
        if (ci > 0x10000 && ci < 0xBFFF0000) {
            uintptr_t base = *(uintptr_t*)(ci + 0x08);
            if (base > 0x10000 && base < 0xBFFF0000) {
                uintptr_t tv = base + (n - 1) * 16;
                if (tv > 0x10000 && tv < 0xBFFF0000) {
                    uintptr_t top = *(uintptr_t*)(L + 0x0C);
                    if (top > 0x10000 && top < 0xBFFF0000) {
                        __try {
                            uint32_t tt = *(uint32_t*)(tv + 8);
                            if (tt > 0 && tt < 10) {
                                uint64_t val = *(uint64_t*)(tv);
                                uint32_t taint = *(uint32_t*)(tv + 12);
                                *(uint64_t*)(top) = val;
                                *(uint32_t*)(top + 8) = tt;
                                *(uint32_t*)(top + 12) = taint;
                                *(uintptr_t*)(L + 0x0C) = top + 16;
                                g_hits++;
                                return (const char*)(tv + 24);
                            }
                        } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    }
                }
            }
        }
    }
    g_misses++;
    return orig(L, ar, n);
}

bool InstallLuaGetLocalFast() {
    void* t = (void*)0x0084F0F0;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    MH_EnableHook(t);
    Log("[GetLocal] ACTIVE — lua_getlocal inline at 0x84F0F0");
    CrashDumper::RegisterFeature("GetLocal");
    CrashDumper::FeatureSetActive("GetLocal", true);
    return true;
}
