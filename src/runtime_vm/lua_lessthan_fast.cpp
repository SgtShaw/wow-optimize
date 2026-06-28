// ============================================================================
// Module: lua_lessthan_fast.cpp
// Description: Accelerates Lua runtime calls in `lua_lessthan_fast.cpp`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================

#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

typedef int(__cdecl *lessthan_fn)(uintptr_t L, int index1, int index2);
static lessthan_fn orig = nullptr;
static volatile long g_hits = 0, g_misses = 0;

static int __cdecl hook(uintptr_t L, int index1, int index2) {
    if (L > 0x10000 && L < 0xBFFF0000) {
        __try {
            uintptr_t base = *(uintptr_t*)(L + 0x10);
            uintptr_t top = *(uintptr_t*)(L + 0x0C);
            if (base > 0x10000 && base < 0xBFFF0000 && top > 0x10000 && top < 0xBFFF0000) {
                uintptr_t tv1 = (index1 > 0) ? (base + (index1 - 1) * 16) : (top + index1 * 16);
                uintptr_t tv2 = (index2 > 0) ? (base + (index2 - 1) * 16) : (top + index2 * 16);
                if (tv1 > 0x10000 && tv1 < 0xBFFF0000 && tv2 > 0x10000 && tv2 < 0xBFFF0000) {
                    uint32_t tt1 = *(uint32_t*)(tv1 + 8);
                    uint32_t tt2 = *(uint32_t*)(tv2 + 8);
                    if (tt1 == 3 && tt2 == 3) {
                        double d1 = *(double*)(tv1);
                        double d2 = *(double*)(tv2);
                        g_hits++;
                        return (d1 < d2) ? 1 : 0;
                    }
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    g_misses++;
    return orig(L, index1, index2);
}

bool InstallLuaLessThanFast() {
    void* t = (void*)0x0084F030;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    MH_EnableHook(t);
    Log("[LessThan] ACTIVE — lua_lessthan inline at 0x84F030");
    CrashDumper::RegisterFeature("LessThan");
    CrashDumper::FeatureSetActive("LessThan", true);
    return true;
}
