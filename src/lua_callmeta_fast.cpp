#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

// luaL_callmeta at 0x84F350 — call metamethod on object at index
// Fast path: inline the getmetafield + call sequence
// Falls back to original for pseudo-indices and missing metatables.
typedef int(__cdecl *callmeta_fn)(uintptr_t L, int obj, const char* event);
static callmeta_fn orig = nullptr;
static volatile long g_hits = 0, g_misses = 0;

static int __cdecl hook(uintptr_t L, int obj, const char* event) {
    if (L < 0x10000 || L > 0xBFFF0000) { g_misses++; return orig(L, obj, event); }
    if (!event) { g_misses++; return orig(L, obj, event); }

    __try {
        // getmetatable(L, obj) — push metatable or return 0
        typedef int(__cdecl *getmeta_fn)(uintptr_t, int);
        int hasMeta = ((getmeta_fn)0x0084E730)(L, obj);
        if (!hasMeta) { g_misses++; return orig(L, obj, event); }

        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        // Check metatable is a table
        if (*(uint32_t*)(top - 8) != 5) {
            *(uintptr_t*)(L + 0x0C) = top - 16;
            g_misses++;
            return orig(L, obj, event);
        }

        // pushstring(L, event) — push event name
        typedef uintptr_t(__cdecl *pushstr_fn)(uintptr_t, const char*);
        ((pushstr_fn)0x0084E400)(L, event);

        // rawget(L, -2) — get metatable[event]
        typedef int(__cdecl *rawget_fn)(uintptr_t, int);
        ((rawget_fn)0x0084E600)(L, -2);

        top = *(uintptr_t*)(L + 0x0C);
        if (!*(uint32_t*)(top - 8)) {
            // nil result: pop metatable + nil (3 slots), return 0
            *(uintptr_t*)(L + 0x0C) = top - 32; // pop key, nil value, metatable
            g_misses++;
            return orig(L, obj, event);
        }

        // Got a function: push obj, call it
        typedef void(__cdecl *pushvalue_fn)(uintptr_t, int);
        ((pushvalue_fn)0x0084DE50)(L, obj);

        // lua_call(L, 1, 1) — call the metamethod
        typedef void(__cdecl *call_fn)(uintptr_t, int, int);
        ((call_fn)0x0084EBF0)(L, 1, 1);

        g_hits++;
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    g_misses++;
    return orig(L, obj, event);
}

bool InstallLuaCallMetaFast() {
    void* t = (void*)0x0084F350;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    WO_EnableHook(t);
    Log("[CallMeta] ACTIVE — luaL_callmeta inline at 0x84F350");
    CrashDumper::RegisterFeature("CallMeta");
    CrashDumper::FeatureSetActive("CallMeta", true);
    return true;
}
