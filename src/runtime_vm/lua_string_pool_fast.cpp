#include <windows.h>
#include <unordered_map>
#include <string>
#include "win_mutex.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace LuaStringPoolFast {

// Interned symbol pool
static std::unordered_map<std::string, void*> g_symbolPool;
static WinMutex g_poolMutex;
static uint64_t g_hits = 0;
static uint64_t g_misses = 0;

void* GetSymbol(const std::string& str) {
    WinLockGuard lock(g_poolMutex);
    auto it = g_symbolPool.find(str);
    if (it != g_symbolPool.end()) {
        g_hits++;
        return it->second;
    }
    g_misses++;
    return nullptr;
}

void InsertSymbol(const std::string& str, void* luaStringObj) {
    WinLockGuard lock(g_poolMutex);
    g_symbolPool[str] = luaStringObj;
}

bool Init() {
    Log("[LuaStringPoolFast] Active - Lua String Symbol Pool Cache Initialized");
    return true;
}

void Shutdown() {
    WinLockGuard lock(g_poolMutex);
    g_symbolPool.clear();
    Log("[LuaStringPoolFast] Stats: %lld hits, %lld misses in string pool", g_hits, g_misses);
}

// Feature 12 (0x0051D9B0): TLS-cached String Table Lookup
void* OptimizeSub51D9B0_TLSString(const char* str, size_t len) {
    if (!str || len == 0) return nullptr;
    static __declspec(thread) const char* t_lastStr = nullptr;
    static __declspec(thread) void* t_lastSymbol = nullptr;
    if (t_lastStr == str && t_lastSymbol) return t_lastSymbol;

    void* sym = GetSymbol(std::string(str, len));
    t_lastStr = str;
    t_lastSymbol = sym;
    return sym;
}

} // namespace LuaStringPoolFast
