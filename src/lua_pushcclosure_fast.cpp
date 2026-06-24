#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

#define TAINT_CELL 0x00D4139C

// lua_pushcclosure at 0x84E400 — push C closure (L, fn, nupvals)
// Fast path: nupvals==0 (equivalent to lua_pushcfunction)
typedef int(__cdecl *pushcclosure_fn)(uintptr_t L, uintptr_t fn, int nupvals);
static pushcclosure_fn orig = nullptr;
static volatile long g_hits = 0, g_misses = 0;

static int __cdecl hook(uintptr_t L, uintptr_t fn, int nupvals) {
    if (L < 0x10000 || L > 0xBFFF0000) { g_misses++; return orig(L, fn, nupvals); }

    if (nupvals != 0) { g_misses++; return orig(L, fn, nupvals); }

    __try {
        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        if (top < 0x10000 || top > 0xBFFF0000) { g_misses++; return orig(L, fn, nupvals); }

        // GC check: if GC threshold exceeded, run a step
        uintptr_t g = *(uintptr_t*)(L + 0x14);
        if (*(uintptr_t*)(g + 0x44) >= *(uintptr_t*)(g + 0x40)) {
            typedef void(__cdecl *gcstep_fn)(uintptr_t);
            ((gcstep_fn)0x0085B950)(L);
        }

        // Resolve the main thread for closure env
        uintptr_t ci = *(uintptr_t*)(L + 0x18);
        uintptr_t proto = 0;
        if (ci == *(uintptr_t*)(L + 0x2C)) {
            proto = *(uintptr_t*)(*(uintptr_t*)(ci + 4) + 16);
        }

        // Allocate CClosure (4 * nupvals + 28 bytes)
        typedef uintptr_t(__cdecl *alloc_fn)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
        uintptr_t cl = ((alloc_fn)0x0085D6F0)(L, 0, 0, 28);
        if (cl < 0x10000) { g_misses++; return orig(L, fn, nupvals); }

        // Set GC header (type 6 = closure)
        typedef void(__cdecl *gcheader_fn)(uintptr_t, uintptr_t, int);
        ((gcheader_fn)0x0085BAB0)(L, cl, 6);

        // Set CClosure fields
        *(uintptr_t*)(cl + 16) = fn;       // f
        *(uint8_t*)(cl + 10) = 0;          // isC = 0 (C closure)
        *(uint8_t*)(cl + 11) = 0;          // nupvalues = 0

        // Set env to default if nil
        if (*(uintptr_t*)(cl + 4) == 0)
            *(uintptr_t*)(cl + 4) = *(uintptr_t*)0x00D413A8;

        // Push onto stack
        uint32_t taint = *(uint32_t*)TAINT_CELL;
        *(uintptr_t*)(top + 0) = cl;
        *(uint32_t*)(top + 4) = 0;
        *(uint32_t*)(top + 8) = 6;         // LUA_TFUNCTION
        *(uint32_t*)(top + 12) = taint;
        *(uintptr_t*)(L + 0x0C) = top + 16;

        g_hits++;
        return (int)L;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    g_misses++;
    return orig(L, fn, nupvals);
}

bool InstallLuaPushCClosureFast() {
    void* t = (void*)0x0084E400;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    WO_EnableHook(t);
    Log("[PushCClos] ACTIVE — lua_pushcclosure inline at 0x84E400");
    CrashDumper::RegisterFeature("PushCClos");
    CrashDumper::FeatureSetActive("PushCClos", true);
    return true;
}
