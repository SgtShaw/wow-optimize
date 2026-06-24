#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

// luaL_getn at 0x84F6C0 — get table length with metatable fallback.
// Engine: normalize pseudo-index → if tt>0 at top → save taint → rawgeti+tonumber
// → if number found rawgeti+setmeta else objlen+1 → restore taint → return.
// Fast path: for plain tables without __len metamethod, return sizearray directly.

typedef int (__cdecl *getn_fn)(uintptr_t L, unsigned int idx);
static getn_fn orig = nullptr;
static volatile long g_hits = 0;

#define LUA_TTABLE 5
static const unsigned int PSEUDO_THRESHOLD = 0xFFFFD8F1u;

static inline uintptr_t get_top(uintptr_t L) { return *(uintptr_t*)(L + 0x0C); }
static inline uintptr_t get_base(uintptr_t L) { return *(uintptr_t*)(L + 0x10); }
static inline uintptr_t index2adr_raw(uintptr_t L, int idx) {
    if (idx > 0) {
        uintptr_t base = get_base(L);
        return base + (idx - 1) * 16;
    }
    if (idx > -10000) {
        uintptr_t top = get_top(L);
        return top + idx * 16;
    }
    return 0;
}
static inline int lua_type_raw(uintptr_t tv, uintptr_t nil_addr) {
    if (tv == nil_addr) return -1;
    return *(int*)(tv + 8);
}

static int __cdecl hook(uintptr_t L, unsigned int idx) {
    if (L < 0x10000 || L > 0xBFFF0000)
        return orig(L, idx);
    __try {
        unsigned int real_idx = idx;
        if (idx >= PSEUDO_THRESHOLD || idx == 0)
            goto fallback;
        uintptr_t top = get_top(L);
        uintptr_t base = get_base(L);
        if (top < 0x10000 || base < 0x10000)
            goto fallback;
        int tt_top = *(int*)(top - 8);
        if (tt_top <= 0)
            goto fallback;

        uintptr_t tv = index2adr_raw(L, real_idx);
        if (!tv || tv < base || tv >= top)
            goto fallback;
        int tt = *(int*)(tv + 8);
        if (tt != LUA_TTABLE)
            goto fallback;
        uintptr_t table = *(uintptr_t*)(tv + 0);
        if (table < 0x10000)
            goto fallback;
        unsigned char lsizenode = *(unsigned char*)(table + 0x0B);
        int is_array = (lsizenode == 0);
        if (!is_array)
            goto fallback;
        uintptr_t metatable = *(uintptr_t*)(table + 0x0C);
        if (metatable != 0)
            goto fallback;
        int sizearray = *(int*)(table + 0x20);
        if (sizearray < 0)
            goto fallback;
        g_hits++;
        return sizearray;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
fallback:
    return orig(L, idx);
}

bool InstallLuaGetnFast() {
    void* t = (void*)0x0084F6C0;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    WO_EnableHook(t);
    Log("[GetnFast] ACTIVE — luaL_getn inline at 0x84F6C0");
    CrashDumper::RegisterFeature("GetnFast");
    CrashDumper::FeatureSetActive("GetnFast", true);
    return true;
}
