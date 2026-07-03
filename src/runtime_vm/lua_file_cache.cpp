// ============================================================================
// Module: lua_file_cache.cpp
// Description: Caches compiled Lua bytecode by filename to skip disk I/O
//              and luac parsing on repeat require()/loadfile() calls.
// Safety & Threading: Main thread only. Cache cleared on lua_State swap.
// ============================================================================

#include <windows.h>
#include <cstdint>
#include <cstring>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"
#include <mimalloc.h>

extern "C" void Log(const char* fmt, ...);

#if !TEST_DISABLE_LUA_FILE_CACHE

// Cache entry: maps filename hash -> compiled bytecode
static constexpr int LUA_FILE_CACHE_SIZE = 512;
static constexpr int LUA_FILE_CACHE_MASK = LUA_FILE_CACHE_SIZE - 1;
static constexpr int MAX_PATH_LEN = 260;
static constexpr int MAX_BYTECODE_SIZE = 256 * 1024; // 256KB max per file

struct LuaFileCacheEntry {
    uint32_t pathHash;
    uint8_t* bytecode;
    uint32_t bytecodeSize;
    bool     valid;
};

static LuaFileCacheEntry g_luaFileCache[LUA_FILE_CACHE_SIZE] = {};
static volatile LONG g_lfcHits = 0;
static volatile LONG g_lfcMisses = 0;
static volatile LONG g_lfcStored = 0;

static inline uint32_t HashPathCI(const char* path) {
    uint32_t h = 0x811C9DC5;
    while (*path) {
        char c = *path++;
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c == '/') c = '\\';
        h ^= (uint8_t)c;
        h *= 0x01000193;
    }
    return h;
}

// Lookup cached bytecode for a given path
const uint8_t* LuaFileCache_Lookup(const char* path, uint32_t* outSize) {
    if (!path || !outSize) return nullptr;

    uint32_t hash = HashPathCI(path);
    int slot = hash & LUA_FILE_CACHE_MASK;
    LuaFileCacheEntry* e = &g_luaFileCache[slot];

    if (e->valid && e->pathHash == hash && e->bytecode && e->bytecodeSize > 0) {
        *outSize = e->bytecodeSize;
        InterlockedIncrement(&g_lfcHits);
        return e->bytecode;
    }

    InterlockedIncrement(&g_lfcMisses);
    return nullptr;
}

// Store compiled bytecode in cache
bool LuaFileCache_Store(const char* path, const uint8_t* bytecode, uint32_t size) {
    if (!path || !bytecode || size == 0 || size > MAX_BYTECODE_SIZE) return false;

    uint32_t hash = HashPathCI(path);
    int slot = hash & LUA_FILE_CACHE_MASK;
    LuaFileCacheEntry* e = &g_luaFileCache[slot];

    // Free old entry if present
    if (e->bytecode) {
        mi_free(e->bytecode);
        e->bytecode = nullptr;
    }

    // Allocate and copy
    e->bytecode = (uint8_t*)mi_malloc(size);
    if (!e->bytecode) return false;

    memcpy(e->bytecode, bytecode, size);
    e->bytecodeSize = size;
    e->pathHash = hash;
    e->valid = true;

    InterlockedIncrement(&g_lfcStored);
    return true;
}

// Clear entire cache (called on lua_State swap / UI reload)
void LuaFileCache_Clear() {
    for (int i = 0; i < LUA_FILE_CACHE_SIZE; i++) {
        if (g_luaFileCache[i].bytecode) {
            mi_free(g_luaFileCache[i].bytecode);
            g_luaFileCache[i].bytecode = nullptr;
        }
        g_luaFileCache[i].valid = false;
        g_luaFileCache[i].bytecodeSize = 0;
    }
    Log("[LuaFileCache] Cleared (%ld hits, %ld misses, %ld stored)",
        g_lfcHits, g_lfcMisses, g_lfcStored);
    g_lfcHits = 0;
    g_lfcMisses = 0;
    g_lfcStored = 0;
}

bool InstallLuaFileCache() {
    memset(g_luaFileCache, 0, sizeof(g_luaFileCache));
    Log("[LuaFileCache] ACTIVE (%d slots, max %d KB/file)",
        LUA_FILE_CACHE_SIZE, MAX_BYTECODE_SIZE / 1024);
    CrashDumper::RegisterFeature("LuaFileCache");
    CrashDumper::FeatureSetActive("LuaFileCache", true);
    return true;
}

void ShutdownLuaFileCache() {
    LuaFileCache_Clear();
}

#else // TEST_DISABLE_LUA_FILE_CACHE

const uint8_t* LuaFileCache_Lookup(const char*, uint32_t*) { return nullptr; }
bool LuaFileCache_Store(const char*, const uint8_t*, uint32_t) { return false; }
void LuaFileCache_Clear() {}
bool InstallLuaFileCache() {
    Log("[LuaFileCache] DISABLED (test toggle)");
    CrashDumper::RegisterFeature("LuaFileCache");
    CrashDumper::FeatureSetActive("LuaFileCache", false);
    return false;
}
void ShutdownLuaFileCache() {}

#endif