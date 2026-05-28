#include "lua_checknumber_cache.h"
#include <windows.h>
#include <MinHook.h>

extern "C" void Log(const char* fmt, ...);

typedef double(__cdecl* lua_checknumber_t)(void* L, int idx);
static lua_checknumber_t orig_lua_checknumber = nullptr;

// TValue structure (Lua 5.1) - 12 bytes total
struct TValue {
    union {
        double n;           // 8 bytes - number value
        void* p;            // pointer
        int b;              // boolean
    } value;
    int tt;                 // 4 bytes - type tag at offset 8
};

// Original helper function to get TValue* from stack index
typedef TValue* (__cdecl* get_tvalue_t)(void* L, int idx);
static get_tvalue_t orig_get_tvalue = (get_tvalue_t)0x0084D9C0;

static double __cdecl hook_lua_checknumber(void* L, int idx) {
    // Get TValue* from stack
    TValue* tv = orig_get_tvalue(L, idx);

    // Fast path: if already a number, return directly
    if (tv && tv->tt == 3) {  // LUA_TNUMBER
        return tv->value.n;
    }

    // Slow path: call original for conversion
    return orig_lua_checknumber(L, idx);
}

bool InstallLuaCheckNumberCache() {
    void* target = (void*)0x0084DF20;
    
    if (MH_CreateHook(target, (void*)hook_lua_checknumber, (void**)&orig_lua_checknumber) != MH_OK) {
        Log("[LuaCheckNumber] MH_CreateHook failed");
        return false;
    }
    
    if (MH_EnableHook(target) != MH_OK) {
        Log("[LuaCheckNumber] MH_EnableHook failed");
        return false;
    }
    
    Log("[LuaCheckNumber] Installed inline type check optimization (1194 callers)");
    return true;
}

void UninstallLuaCheckNumberCache() {
    MH_DisableHook((void*)0x0084DF20);
}
