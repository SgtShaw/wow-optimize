// ================================================================
// Lua Stack Push/Query Fast Paths — trivially-inlinable C-API hooks
// ================================================================
// Eight functions, each ≤45 bytes in the engine. Inlining them
// eliminates the call/ret overhead + MinHook trampoline jump, and
// more importantly skips the index2adr call for type queries that
// use plain stack indices.
//
// All hooks verified against the stock 3.3.5a binary.
// SEH-guarded, teardown-state-checked, gated behind a single
// TEST_DISABLE flag.
//
// Hooks:
//   lua_pushnil           (0x84E280, 31B) — top[tt]=0, taint, advance
//   lua_pushinteger        (0x84E2D0, 36B) — (double)n → top, advance
//   lua_pushboolean        (0x84E4D0, 41B) — int(bool) → top, advance
//   lua_pushlightuserdata  (0x84E500, 36B) — ptr → top, tt=2, advance
//   lua_type               (0x84DEB0, 31B) — resolve idx → tt
//   lua_isfunction         (0x84DEF0, 27B) — resolve idx, tt==6 && !dead
//   lua_isstring           (0x84DF60, 45B) — resolve idx, tt==4||tt==3
//   lua_tothread           (0x84E1F0, 28B) — resolve idx, tt==8?val:0
// ================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "lua_stack_fast.h"

extern "C" void Log(const char* fmt, ...);

// ================================================================
// Shared: symbolic type constants
// ================================================================
#define LUA_TNIL              0
#define LUA_TBOOLEAN          1
#define LUA_TLIGHTUSERDATA    2
#define LUA_TNUMBER           3
#define LUA_TSTRING           4
#define LUA_TTABLE            5
#define LUA_TFUNCTION         6
#define LUA_TUSERDATA         7
#define LUA_TTHREAD           8

// WoW taint cell (global read-only from the hook — the real functions
// READ it, never write it, except lua_pushnil which writes 0 for tt
// but reads the taint cell)
static const uint32_t TAINT_CELL = 0x00D4139C;

// Pseudo-index boundary
#define LUA_REGISTRYINDEX (-10000)

// nil sentinel (the object returned for invalid indices)
static const uintptr_t NIL_OBJECT = 0x00A46F78;

// Teardown guard: if the lua_State global is zero, the Lua VM is
// being torn down and stack pointers are stale.
extern "C" bool LuaOpt_IsTeardown();
static inline bool IsTeardownState() {
    uintptr_t gL = *(uintptr_t*)0x00D3F78C;
    return (gL < 0x10000 || gL > 0xBFFF0000);
}

// Pointer sanity
static __forceinline bool IsValidPtr(uintptr_t p) {
    return p > 0x10000 && p < 0xBFFF0000;
}

// ================================================================
// Shared inline index resolution (positive/negative stack indices
// only; pseudo-indices defer to the original)
// ================================================================
static __forceinline uintptr_t ResolveTValue(
    uintptr_t L, int idx, bool* deferToOrig)
{
    *deferToOrig = false;
    if (idx > 0) {
        uintptr_t base = *(uintptr_t*)(L + 0x10);
        if (!IsValidPtr(base)) return 0;
        uintptr_t tv = base + (uintptr_t)(idx - 1) * 16;
        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        if (tv >= top) return 0;
        return tv;
    }
    if (idx > LUA_REGISTRYINDEX) {
        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        if (!IsValidPtr(top)) return 0;
        uintptr_t tv = top + (uintptr_t)idx * 16;
        uintptr_t base = *(uintptr_t*)(L + 0x10);
        if (tv < base) return 0;
        return tv;
    }
    // Pseudo-index — can't inline
    *deferToOrig = true;
    return 0;
}

// ================================================================
// 1. lua_pushnil — 0x84E280 (31B)
//    top[0..1]=0, top[2]=0 (LUA_TNIL), top[3]=taint_cell, top+=16
// ================================================================
typedef int (__cdecl *pushnil_t)(uintptr_t L);
static pushnil_t orig_pushnil = nullptr;

static int __cdecl hook_pushnil(uintptr_t L) {
    if (IsTeardownState()) return orig_pushnil(L);
    __try {
        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        if (!IsValidPtr(top)) return orig_pushnil(L);
        uint32_t taint = *(uint32_t*)TAINT_CELL;
        *(uint32_t*)(top + 0) = 0;
        *(uint32_t*)(top + 4) = 0;
        *(uint32_t*)(top + 8) = 0;
        *(uint32_t*)(top + 12) = taint;
        *(uintptr_t*)(L + 0x0C) = top + 16;
        return (int)L;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return orig_pushnil(L);
}

// ================================================================
// 2. lua_pushinteger — 0x84E2D0 (36B)
//    top[0..1]=double(n), top[2]=3 (LUA_TNUMBER), top[3]=taint, top+=16
// ================================================================
typedef int (__cdecl *pushinteger_t)(uintptr_t L, int n);
static pushinteger_t orig_pushinteger = nullptr;

static int __cdecl hook_pushinteger(uintptr_t L, int n) {
    if (IsTeardownState()) return orig_pushinteger(L, n);
    __try {
        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        if (!IsValidPtr(top)) return orig_pushinteger(L, n);
        uint32_t taint = *(uint32_t*)TAINT_CELL;
        double dn = (double)n;
        *(double*)(top + 0) = dn;
        *(uint32_t*)(top + 8) = LUA_TNUMBER;
        *(uint32_t*)(top + 12) = taint;
        *(uintptr_t*)(L + 0x0C) = top + 16;
        return (int)top;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return orig_pushinteger(L, n);
}

// ================================================================
// 3. lua_pushboolean — 0x84E4D0 (41B)
//    top[0]=int(bool), top[2]=1 (LUA_TBOOLEAN), top[3]=taint, top+=16
// ================================================================
typedef int (__cdecl *pushboolean_t)(uintptr_t L, int b);
static pushboolean_t orig_pushboolean = nullptr;

static int __cdecl hook_pushboolean(uintptr_t L, int b) {
    if (IsTeardownState()) return orig_pushboolean(L, b);
    __try {
        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        if (!IsValidPtr(top)) return orig_pushboolean(L, b);
        uint32_t taint = *(uint32_t*)TAINT_CELL;
        *(uint32_t*)(top + 0) = (b != 0) ? 1 : 0;
        *(uint32_t*)(top + 4) = 0;
        *(uint32_t*)(top + 8) = LUA_TBOOLEAN;
        *(uint32_t*)(top + 12) = taint;
        *(uintptr_t*)(L + 0x0C) = top + 16;
        return (int)top;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return orig_pushboolean(L, b);
}

// ================================================================
// 4. lua_pushlightuserdata — 0x84E500 (36B)
//    top[0]=ptr, top[2]=2 (LUA_TLIGHTUSERDATA), top[3]=taint, top+=16
// ================================================================
typedef int (__cdecl *pushlightuserdata_t)(uintptr_t L, uintptr_t p);
static pushlightuserdata_t orig_pushlightuserdata = nullptr;

static int __cdecl hook_pushlightuserdata(uintptr_t L, uintptr_t p) {
    if (IsTeardownState()) return orig_pushlightuserdata(L, p);
    __try {
        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        if (!IsValidPtr(top)) return orig_pushlightuserdata(L, p);
        uint32_t taint = *(uint32_t*)TAINT_CELL;
        *(uintptr_t*)(top + 0) = p;
        *(uint32_t*)(top + 8) = LUA_TLIGHTUSERDATA;
        *(uint32_t*)(top + 12) = taint;
        *(uintptr_t*)(L + 0x0C) = top + 16;
        return (int)top;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return orig_pushlightuserdata(L, p);
}

// ================================================================
// 5. lua_type — 0x84DEB0 (31B)
//    Resolves idx → tt (nil=0 returns -1, valid returns tt field)
// ================================================================
typedef int (__cdecl *lua_type_t)(uintptr_t L, int idx);
static lua_type_t orig_lua_type = nullptr;

static int __cdecl hook_lua_type(uintptr_t L, int idx) {
    if (IsTeardownState()) return orig_lua_type(L, idx);
    __try {
        bool defer = false;
        uintptr_t tv = ResolveTValue(L, idx, &defer);
        if (defer) return orig_lua_type(L, idx);
        if (tv) {
            if (tv == NIL_OBJECT) return -1;
            return *(int*)(tv + 8);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return orig_lua_type(L, idx);
}

// ================================================================
// 6. lua_isfunction — 0x84DEF0 (27B)
//    Resolves idx → returns 1 if tt==6 AND function is not dead
// ================================================================
typedef int (__cdecl *lua_isfunc_t)(uintptr_t L, int idx);
static lua_isfunc_t orig_lua_isfunction = nullptr;

static int __cdecl hook_lua_isfunction(uintptr_t L, int idx) {
    if (IsTeardownState()) return orig_lua_isfunction(L, idx);
    __try {
        bool defer = false;
        uintptr_t tv = ResolveTValue(L, idx, &defer);
        if (defer) return orig_lua_isfunction(L, idx);
        if (tv && tv != NIL_OBJECT && *(int*)(tv + 8) == LUA_TFUNCTION) {
            // The engine checks: tt==6 && *(byte*)(*(int*)tv + 10)
            uintptr_t gc = *(uintptr_t*)(tv + 0);
            if (IsValidPtr(gc)) {
                return (*(uint8_t*)(gc + 10) != 0) ? 1 : 0;
            }
        }
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return orig_lua_isfunction(L, idx);
}

// ================================================================
// 7. lua_isstring — 0x84DF60 (45B)
//    Resolves idx → returns 1 if tt==4||tt==3
// ================================================================
typedef int (__cdecl *lua_isstring_t)(uintptr_t L, int idx);
static lua_isstring_t orig_lua_isstring = nullptr;

static int __cdecl hook_lua_isstring(uintptr_t L, int idx) {
    if (IsTeardownState()) return orig_lua_isstring(L, idx);
    __try {
        bool defer = false;
        uintptr_t tv = ResolveTValue(L, idx, &defer);
        if (defer) return orig_lua_isstring(L, idx);
        if (tv && tv != NIL_OBJECT) {
            int tt = *(int*)(tv + 8);
            if (tt == LUA_TSTRING || tt == LUA_TNUMBER)
                return 1;
        }
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return orig_lua_isstring(L, idx);
}

// ================================================================
// 8. lua_tothread — 0x84E1F0 (28B)
//    Resolves idx → if tt==8 return *tv (the lua_State pointer), else 0
// ================================================================
typedef int (__cdecl *lua_tothread_t)(uintptr_t L, int idx);
static lua_tothread_t orig_lua_tothread = nullptr;

static int __cdecl hook_lua_tothread(uintptr_t L, int idx) {
    if (IsTeardownState()) return orig_lua_tothread(L, idx);
    __try {
        bool defer = false;
        uintptr_t tv = ResolveTValue(L, idx, &defer);
        if (defer) return orig_lua_tothread(L, idx);
        if (tv && tv != NIL_OBJECT && *(int*)(tv + 8) == LUA_TTHREAD) {
            return *(int*)(tv + 0);
        }
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return orig_lua_tothread(L, idx);
}

// ================================================================
// Install / Shutdown
// ================================================================

static void* const ADDR_PUSHNIL           = (void*)0x0084E280;
static void* const ADDR_PUSHINTEGER       = (void*)0x0084E2D0;
static void* const ADDR_PUSHBOOLEAN       = (void*)0x0084E4D0;
static void* const ADDR_PUSHLIGHTUSERDATA = (void*)0x0084E500;
static void* const ADDR_LUA_TYPE          = (void*)0x0084DEB0;
static void* const ADDR_LUA_ISFUNCTION    = (void*)0x0084DEF0;
static void* const ADDR_LUA_ISSTRING      = (void*)0x0084DF60;
static void* const ADDR_LUA_TOTHREAD      = (void*)0x0084E1F0;

bool InstallLuaStackFast() {
    int installed = 0;
    MH_STATUS st;

    #define INSTALL(type, orig_ptr, detour, addr, name) \
        st = WineSafe_CreateHook(addr, (void*)detour, (void**)&orig_ptr); \
        if (st == MH_OK) { MH_EnableHook(addr); installed++; \
            Log("[LuaStackFast] %s ACTIVE (%p)", name, addr); } \
        else { Log("[LuaStackFast] %s FAILED (status %d)", name, (int)st); }

    INSTALL(pushnil_t,           orig_pushnil,           hook_pushnil,           ADDR_PUSHNIL,           "lua_pushnil");
    INSTALL(pushinteger_t,       orig_pushinteger,       hook_pushinteger,       ADDR_PUSHINTEGER,       "lua_pushinteger");
    INSTALL(pushboolean_t,       orig_pushboolean,       hook_pushboolean,       ADDR_PUSHBOOLEAN,       "lua_pushboolean");
    INSTALL(pushlightuserdata_t, orig_pushlightuserdata, hook_pushlightuserdata, ADDR_PUSHLIGHTUSERDATA, "lua_pushlightuserdata");
    INSTALL(lua_type_t,         orig_lua_type,          hook_lua_type,          ADDR_LUA_TYPE,          "lua_type");
    INSTALL(lua_isfunc_t,       orig_lua_isfunction,    hook_lua_isfunction,    ADDR_LUA_ISFUNCTION,    "lua_isfunction");
    INSTALL(lua_isstring_t,     orig_lua_isstring,      hook_lua_isstring,      ADDR_LUA_ISSTRING,      "lua_isstring");
    INSTALL(lua_tothread_t,     orig_lua_tothread,      hook_lua_tothread,      ADDR_LUA_TOTHREAD,      "lua_tothread");

    #undef INSTALL

    Log("[LuaStackFast] %d/8 hooks installed", installed);
    return installed > 0;
}

void ShutdownLuaStackFast() {
    MH_DisableHook(ADDR_PUSHNIL);
    MH_DisableHook(ADDR_PUSHINTEGER);
    MH_DisableHook(ADDR_PUSHBOOLEAN);
    MH_DisableHook(ADDR_PUSHLIGHTUSERDATA);
    MH_DisableHook(ADDR_LUA_TYPE);
    MH_DisableHook(ADDR_LUA_ISFUNCTION);
    MH_DisableHook(ADDR_LUA_ISSTRING);
    MH_DisableHook(ADDR_LUA_TOTHREAD);
}