// ================================================================
// lua_rawget Safe Fast Path
// ================================================================
// Replaces sub_84E600 (lua_rawget) with an optimized version that:
//
// 1. Direct stack index resolution for base and top
// 2. Direct call to luaH_get (0x0085C470) to retrieve the value
// 3. Replaces the key on the stack top with the value in place
// 4. Correctly propagates Lua VM taint matching the engine
// ================================================================

#include "lua_rawget_inline.h"
#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"
#include "lua_optimize.h"

extern "C" void Log(const char* fmt, ...);

// Statistics
static volatile long g_rawgetCalls = 0;
static volatile long g_rawgetFast  = 0;

static const uint32_t TAINT_CELL = 0x00D4139C;

typedef int (__cdecl *lua_rawget_fn)(uintptr_t L, int idx);
static lua_rawget_fn orig_rawget = nullptr;

// Resolve stack index to TValue pointer
static __forceinline bool IsValidPtr(uintptr_t p) {
    return p > 0x10000 && p < 0xBFFF0000;
}

static __forceinline uintptr_t ResolveIndex(uintptr_t L, int idx) {
    if (idx > 0) {
        uintptr_t base = *(uintptr_t*)(L + 0x10);
        if (!IsValidPtr(base)) return 0;
        uintptr_t tv = base + (uintptr_t)(idx - 1) * 16;
        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        if (tv >= top) return 0;
        return tv;
    }
    if (idx > -10000) { // LUA_REGISTRYINDEX (-10000)
        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        if (!IsValidPtr(top)) return 0;
        uintptr_t tv = top + (uintptr_t)idx * 16;
        uintptr_t base = *(uintptr_t*)(L + 0x10);
        if (tv < base) return 0;
        return tv;
    }
    return 0;
}

// Hooked lua_rawget (0x84E600): reads table at idx, lookup using key at L->top - 1,
// replaces the key at L->top - 1 with the retrieved value. Stack height doesn't change.
static int __cdecl Hooked_RawGet(uintptr_t L, int idx) {
    ++g_rawgetCalls;

    if (LuaOpt::IsReloading() || LuaOpt::IsSwapping()) {
        return orig_rawget(L, idx);
    }

    if (L < 0x10000 || L > 0xBFFF0000) {
        return orig_rawget(L, idx);
    }

    __try {
        uintptr_t t_tv = ResolveIndex(L, idx);
        if (t_tv && *(int*)(t_tv + 8) == 5) { // LUA_TTABLE
            uintptr_t top = *(uintptr_t*)(L + 0x0C);
            uintptr_t key = top - 16;
            uintptr_t base = *(uintptr_t*)(L + 0x10);
            if (IsValidPtr(top) && IsValidPtr(base) && key >= base) {
                uintptr_t tab = *(uintptr_t*)(t_tv + 0);
                if (IsValidPtr(tab)) {
                    // Call the engine's luaH_get (0x0085C470)
                    typedef uintptr_t (__cdecl *luaH_get_t)(uintptr_t t, uintptr_t key);
                    luaH_get_t luaH_get = (luaH_get_t)0x0085C470;
                    uintptr_t val = luaH_get(tab, key);
                    if (IsValidPtr(val)) {
                        // Replace key at stack top with value
                        *(uint64_t*)(key + 0) = *(uint64_t*)(val + 0);
                        *(int*)(key + 8) = *(int*)(val + 8);
                        
                        // Handle taint
                        uint32_t taint = *(uint32_t*)TAINT_CELL;
                        if (*(int*)(val + 8) == 0) { // LUA_TNIL
                            *(uint32_t*)(key + 12) = taint;
                        } else {
                            if (*(uint32_t*)0x00D413A0 && !*(uint32_t*)0x00D413A4) {
                                *(uint32_t*)TAINT_CELL = *(uint32_t*)(val + 12);
                            }
                            *(uint32_t*)(key + 12) = *(uint32_t*)(val + 12);
                        }
                        
                        ++g_rawgetFast;
                        return 1;
                    }
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    return orig_rawget(L, idx);
}

bool InstallLuaRawGetInline() {
    void* target = (void*)0x0084E600;
    unsigned char* p = (unsigned char*)target;
    if (p[0] != 0x55 || p[1] != 0x8B) {
        Log("[LuaRawGet] BAD PROLOGUE at 0x%08X (expected 55 8B)", (uintptr_t)target);
        return false;
    }
    if (MH_CreateHook(target, (void*)Hooked_RawGet, (void**)&orig_rawget) != MH_OK) {
        Log("[LuaRawGet] MH_CreateHook FAILED");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[LuaRawGet] MH_EnableHook FAILED");
        return false;
    }
    Log("[LuaRawGet] ACTIVE: inline lua_rawget (0x84E600)");
    CrashDumper::RegisterFeature("LuaRawGet");
    CrashDumper::FeatureSetActive("LuaRawGet", true);
    return true;
}

void UninstallLuaRawGetInline() {
    MH_DisableHook((void*)0x0084E600);
    MH_RemoveHook((void*)0x0084E600);
    CrashDumper::FeatureSetActive("LuaRawGet", false);
    LONG64 total = g_rawgetCalls;
    LONG64 fast  = g_rawgetFast;
    if (total > 0) {
        Log("[LuaRawGet] Stats: %lld calls, %lld inline (%.1f%%)",
            (long long)total, (long long)fast,
            100.0 * (double)fast / (double)total);
    }
}
