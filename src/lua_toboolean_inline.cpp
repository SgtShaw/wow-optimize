#include "lua_toboolean_inline.h"
#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

// Statistics (plain increments: Lua is single-threaded)
static volatile long g_tobooleanCalls = 0;
static volatile long g_tobooleanFast  = 0;

// Original function pointer
typedef int (__cdecl *lua_toboolean_fn)(int L, int idx);
static lua_toboolean_fn orig_toboolean = nullptr;

// WoW's lua_toboolean (0x84E0B0): reads tt from the indexed TValue,
// returns 1 if tt != 0 (nil/false = 0, everything else = 1).
// Replacement: inline the TValue-type check, skip the engine dispatch.
static int __cdecl Hooked_Toboolean(int L, int idx) {
    ++g_tobooleanCalls;
    __try {
        int* base = *(int**)(L + 0x10);  // L->base
        int* top  = *(int**)(L + 0x0C);  // L->top

        int* slot = nullptr;
        if (idx > 0) {
            slot = base + (idx - 1) * 4;
            if (slot >= top) goto fallback;
        } else if (idx >= -10000) {
            slot = top + idx * 4;
            if (slot < base) goto fallback;
        } else if (idx == -10002) {
            slot = base + 18 * 4;  // GLOBALSINDEX
        }

        if (!slot || (uintptr_t)slot < 0x10000 || (uintptr_t)slot > 0xBFFF0000)
            goto fallback;

        int tt = slot[2];        // TValue.tt at +8
        int taint = slot[3];     // TValue.taint at +12

        ++g_tobooleanFast;
        // Original: returns 1 if tt != 0 (any non-nil type), 0 otherwise
        // Also propagates taint (if taint != 0 and byte_D413A0 set, store it)
        if (taint && *(int*)0x00D413A0 && !*(int*)0x00D413A4)
            *(int*)0x00D4139C = taint;

        return (tt != 0) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
fallback:
    return orig_toboolean(L, idx);
}

bool InstallLuaTobooleanInline() {
    void* target = (void*)0x0084E0B0;
    unsigned char* p = (unsigned char*)target;
    if (p[0] != 0x55 || p[1] != 0x8B) {
        Log("[LuaTBool] BAD PROLOGUE at 0x%08X (expected 55 8B)", (uintptr_t)target);
        return false;
    }
    if (MH_CreateHook(target, (void*)Hooked_Toboolean, (void**)&orig_toboolean) != MH_OK) {
        Log("[LuaTBool] MH_CreateHook FAILED");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[LuaTBool] MH_EnableHook FAILED");
        return false;
    }
    Log("[LuaTBool] ACTIVE: inline lua_toboolean (0x84E0B0)");
    return true;
}

void UninstallLuaTobooleanInline() {
    MH_DisableHook((void*)0x0084E0B0);
    MH_RemoveHook((void*)0x0084E0B0);
    LONG64 total = g_tobooleanCalls;
    LONG64 fast  = g_tobooleanFast;
    if (total > 0) {
        Log("[LuaTBool] Stats: %lld calls, %lld inline (%.1f%%)",
            (long long)total, (long long)fast,
            100.0 * (double)fast / (double)total);
    }
}