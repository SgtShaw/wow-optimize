#include <windows.h>
#include <cstdint>
#include <cstring>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

#define TAINT_CELL 0x00D4139C

// lua_pushstring at 0x84E350 — push string onto stack (L, s)
// NULL → pushes nil; non-NULL → pushes interned string
typedef int(__cdecl *pushstring_fn)(uintptr_t L, const char* s);
static pushstring_fn orig = nullptr;
static volatile long g_hits = 0, g_misses = 0;

static size_t my_strlen(const char* s) {
    const char* p = s;
    while (*p) ++p;
    return (size_t)(p - s);
}

static int __cdecl hook(uintptr_t L, const char* s) {
    if (L < 0x10000 || L > 0xBFFF0000) { g_misses++; return orig(L, s); }

    // NULL string → push nil — defer to original (simpler)
    if (!s) { g_misses++; return orig(L, s); }

    __try {
        if ((uintptr_t)s < 0x10000 || (uintptr_t)s > 0xBFFF0000) { g_misses++; return orig(L, s); }

        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        if (top < 0x10000 || top > 0xBFFF0000) { g_misses++; return orig(L, s); }

        // GC check
        uintptr_t g = *(uintptr_t*)(L + 0x14);
        if (*(uintptr_t*)(g + 0x44) >= *(uintptr_t*)(g + 0x40)) {
            typedef void(__cdecl *gcstep_fn)(uintptr_t);
            ((gcstep_fn)0x0085B950)(L);
        }

        size_t len = my_strlen(s);

        // Intern string via luaS_newlstr
        typedef uintptr_t(__cdecl *newlstr_fn)(uintptr_t, const char*, size_t);
        uintptr_t ts = ((newlstr_fn)0x00856C80)(L, s, len);
        if (ts < 0x10000 || ts > 0xBFFF0000) { g_misses++; return orig(L, s); }

        // Push onto stack
        uint32_t taint = *(uint32_t*)TAINT_CELL;
        *(uintptr_t*)(top + 0) = ts;
        *(uint32_t*)(top + 4) = 0;
        *(uint32_t*)(top + 8) = 4;         // LUA_TSTRING
        *(uint32_t*)(top + 12) = taint;
        *(uintptr_t*)(L + 0x0C) = top + 16;

        g_hits++;
        return (int)ts;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    g_misses++;
    return orig(L, s);
}

bool InstallLuaPushStringFast() {
    void* t = (void*)0x0084E350;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    WO_EnableHook(t);
    Log("[PushString] ACTIVE — lua_pushstring inline at 0x84E350");
    CrashDumper::RegisterFeature("PushString");
    CrashDumper::FeatureSetActive("PushString", true);
    return true;
}
