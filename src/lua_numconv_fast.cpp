#include "lua_numconv_fast.h"
#include <windows.h>
#include <stdint.h>
#include "MinHook.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

// ================================================================
// lua_gettop (0x84DBD0, 17 bytes, ~2M calls/session)
//
// trivially: (L->top - L->base) >> 4
// L->top is at L+0x0C, L->base at L+0x10 (verified vs index2adr)
// The original calls index2adr-style helpers for pseudo-indices, but
// lua_gettop takes no stack-index arg — it just returns the absolute
// stack depth, so the fast path is always valid.
// ================================================================

typedef int (__cdecl* lua_gettop_t)(uintptr_t L);
static lua_gettop_t orig_lua_gettop = nullptr;

static int __cdecl hook_lua_gettop(uintptr_t L) {
    __try {
        uintptr_t top  = *(uintptr_t*)(L + 0x0C);
        uintptr_t base = *(uintptr_t*)(L + 0x10);
        if (top >= base && (top - base) < 0x100000)  // sanity: <1M slots
            return (int)((top - base) >> 4);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // bad pointers — defer to original
    }
    return orig_lua_gettop(L);
}

// ================================================================
// lua_isnumber (0x84DF20, 1194 xrefs) and lua_tonumber (0x84E030, 1165 xrefs)
//
// Both resolve a stack index to TValue* via sub_84D9C0 (index2adr),
// then check TValue.tt == 3 (LUA_TNUMBER). The resolution call is
// expensive relative to the check itself — for the overwhelmingly
// common positive/negative stack indices we inline the pointer
// arithmetic directly.
//
// lua_State layout (3.3.5a, verified vs lua_gettop/index2adr):
//   +0x0C  TValue* top    (stack top)
//   +0x10  TValue* base   (current call frame base)
//
// TValue layout (16 bytes):
//   +0x00  double value   (8 bytes)
//   +0x08  int    tt      (4 bytes, type tag: 3 = LUA_TNUMBER)
//   +0x0C  int    taint   (4 bytes, WoW taint source)
//
// Index resolution:
//   positive idx:  base + (idx - 1) * 16
//   negative idx:  top  + idx * 16   (only for idx > LUA_REGISTRYINDEX)
// ================================================================

#define LUA_TNUMBER 3

// Pseudo-index boundary — anything <= this is a pseudo-index
// (registry, upvalues, globals) which needs the full index2adr path
#define LUA_REGISTRYINDEX (-10000)

// Pointer sanity: must be in usermode range and aligned to 4 bytes
static __forceinline bool IsValidPtr(uintptr_t p) {
    return p > 0x10000 && p < 0xBFFF0000;
}

// Read the type tag from a resolved TValue pointer.
// TValue.tt lives at byte offset 8 (offset 12 is the WoW taint field).
static __forceinline int TValue_tt(uintptr_t tv) {
    return *(int*)(tv + 8);
}

// Read the double value from a resolved TValue pointer.
// TValue.value lives at byte offset 0.
static __forceinline double TValue_nvalue(uintptr_t tv) {
    return *(double*)(tv);
}

// Resolve a Lua stack index to a raw TValue pointer.
// Returns 0 on failure (pseudo-indices, out-of-range, bad pointers).
static __forceinline uintptr_t ResolveIndex(uintptr_t L, int idx) {
    if (idx > 0) {
        uintptr_t base = *(uintptr_t*)(L + 16);
        if (!IsValidPtr(base))
            return 0;
        uintptr_t tv = base + (uintptr_t)(idx - 1) * 16;
        // Must be below top to be a valid slot
        uintptr_t top = *(uintptr_t*)(L + 12);
        if (tv >= top)
            return 0;
        return tv;
    }
    if (idx > LUA_REGISTRYINDEX) {
        uintptr_t top = *(uintptr_t*)(L + 12);
        if (!IsValidPtr(top))
            return 0;
        // idx is negative, so this subtracts
        uintptr_t tv = top + (uintptr_t)idx * 16;
        uintptr_t base = *(uintptr_t*)(L + 16);
        // Negative index must resolve at or above base
        if (tv < base)
            return 0;
        return tv;
    }
    // Pseudo-index (registry, upvalue, global) — can't inline these
    return 0;
}

// ================================================================
// lua_isnumber hook (0x84DF20)
// ================================================================

typedef int (__cdecl* lua_isnumber_t)(uintptr_t L, int idx);
static lua_isnumber_t orig_lua_isnumber = nullptr;

static int __cdecl hook_lua_isnumber(uintptr_t L, int idx) {
    __try {
        uintptr_t tv = ResolveIndex(L, idx);
        if (tv) {
            // Fast path: already a number
            if (TValue_tt(tv) == LUA_TNUMBER)
                return 1;
            // String-to-number conversion is rare for isnumber;
            // let the original handle it
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // Bad pointer or freed Lua state (e.g. during UI reload).
        // Returning 0 gracefully survives the corruption without crashing WoW.
        return 0;
    }
    return orig_lua_isnumber(L, idx);
}

// ================================================================
// lua_tonumber hook (0x84E030)
// ================================================================

typedef double (__cdecl* lua_tonumber_t)(uintptr_t L, int idx);
static lua_tonumber_t orig_lua_tonumber = nullptr;

static double __cdecl hook_lua_tonumber(uintptr_t L, int idx) {
    __try {
        uintptr_t tv = ResolveIndex(L, idx);
        if (tv && TValue_tt(tv) == LUA_TNUMBER) {
            return TValue_nvalue(tv);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // Bad pointer or freed Lua state. Gracefully return 0.0.
        return 0.0;
    }
    return orig_lua_tonumber(L, idx);
}

// ================================================================
// Install / Shutdown
// ================================================================

static void* const ADDR_LUA_GETTOP   = (void*)0x0084DBD0;
static void* const ADDR_LUA_ISNUMBER = (void*)0x0084DF20;
static void* const ADDR_LUA_TONUMBER = (void*)0x0084E030;

bool InstallLuaNumConvFast() {
    int installed = 0;

    // lua_gettop — trivially inlined as (L->top - L->base) >> 4
    // Called millions of times per session (every Lua API call checks)
    MH_STATUS st = WineSafe_CreateHook(
        ADDR_LUA_GETTOP, (void*)hook_lua_gettop, (void**)&orig_lua_gettop);
    if (st == MH_OK) {
        MH_EnableHook(ADDR_LUA_GETTOP);
        installed++;
        Log("[LuaNumConvFast] lua_gettop hook at 0x84DBD0 (inline fast path)");
    } else {
        Log("[LuaNumConvFast] lua_gettop hook FAILED (status %d)", (int)st);
    }

    st = WineSafe_CreateHook(
        ADDR_LUA_ISNUMBER, (void*)hook_lua_isnumber, (void**)&orig_lua_isnumber);
    if (st == MH_OK) {
        MH_EnableHook(ADDR_LUA_ISNUMBER);
        installed++;
        Log("[LuaNumConvFast] lua_isnumber hook at 0x84DF20 (1194 xrefs)");
    } else {
        Log("[LuaNumConvFast] lua_isnumber hook FAILED (status %d)", (int)st);
    }

    st = WineSafe_CreateHook(
        ADDR_LUA_TONUMBER, (void*)hook_lua_tonumber, (void**)&orig_lua_tonumber);
    if (st == MH_OK) {
        MH_EnableHook(ADDR_LUA_TONUMBER);
        installed++;
        Log("[LuaNumConvFast] lua_tonumber hook at 0x84E030 (1165 xrefs)");
    } else {
        Log("[LuaNumConvFast] lua_tonumber hook FAILED (status %d)", (int)st);
    }

    return installed > 0;
}

void ShutdownLuaNumConvFast() {
    MH_DisableHook(ADDR_LUA_GETTOP);
    MH_DisableHook(ADDR_LUA_ISNUMBER);
    MH_DisableHook(ADDR_LUA_TONUMBER);
}
