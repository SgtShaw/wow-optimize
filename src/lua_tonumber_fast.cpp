#include "lua_tonumber_fast.h"
#include "version.h"
#include "MinHook.h"
#include <atomic>

// Forward declaration for Log
extern "C" void Log(const char* fmt, ...);

#if !TEST_DISABLE_LUA_TONUMBER_FAST

// Lua TValue structure (16 bytes)
struct TValue {
    double value;      // 8 bytes - number value
    int type;          // 4 bytes - type tag
    int padding;       // 4 bytes
};

// Lua state structure (simplified)
struct lua_State {
    char padding1[16];     // 0x00-0x0F
    TValue* stack;         // 0x10 - stack base
    TValue* top;           // 0x14 - stack top
    // ... more fields
};

// Original function pointer
typedef double (__cdecl* lua_tonumber_t)(lua_State* L, int idx);
static lua_tonumber_t orig_lua_tonumber = nullptr;

// Statistics
static std::atomic<uint64_t> g_fast_path_count{0};
static std::atomic<uint64_t> g_slow_path_count{0};

// Helper to get TValue from stack index (inlined from sub_84D9C0)
static inline TValue* lua_index2adr(lua_State* L, int idx) {
    if (idx > 0) {
        TValue* o = L->stack + idx - 1;
        return (o < L->top) ? o : nullptr;
    } else if (idx > -10000) {  // LUA_MINSTACK
        return L->top + idx;
    }
    return nullptr;  // pseudo-index or invalid
}

// Hooked lua_tonumber with fast path
double __cdecl hooked_lua_tonumber(lua_State* L, int idx) {
    // Fast path: inline type check and value extraction
    TValue* o = lua_index2adr(L, idx);
    
    if (o && o->type == 4) {  // LUA_TNUMBER = 4
        // Number type - return value directly
        g_fast_path_count++;
        return o->value;
    }
    
    // Slow path: call original for type conversion
    g_slow_path_count++;
    return orig_lua_tonumber(L, idx);
}

bool InstallLuaToNumberFast() {
    void* target = (void*)0x0084E0E0;  // lua_tonumber
    
    if (MH_CreateHook(target, (void*)hooked_lua_tonumber, (void**)&orig_lua_tonumber) != MH_OK) {
        Log("[LuaToNumberFast] MH_CreateHook failed");
        return false;
    }
    
    if (MH_EnableHook(target) != MH_OK) {
        Log("[LuaToNumberFast] MH_EnableHook failed");
        return false;
    }
    
    Log("[LuaToNumberFast] Installed hook at 0x%08X (750 xrefs)", target);
    return true;
}

void GetLuaToNumberStats(uint64_t* fast_path, uint64_t* slow_path) {
    if (fast_path) *fast_path = g_fast_path_count.load();
    if (slow_path) *slow_path = g_slow_path_count.load();
}

#else  // TEST_DISABLE_LUA_TONUMBER_FAST

bool InstallLuaToNumberFast() {
    Log("[LuaToNumberFast] Disabled via feature flag");
    return false;
}

void GetLuaToNumberStats(uint64_t* fast_path, uint64_t* slow_path) {
    if (fast_path) *fast_path = 0;
    if (slow_path) *slow_path = 0;
}

#endif
