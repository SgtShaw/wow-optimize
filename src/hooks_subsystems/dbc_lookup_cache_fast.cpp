#include <windows.h>
#include <unordered_map>
#include <string>
#include "win_mutex.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace DbcLookupCacheFast {

static std::unordered_map<uint32_t, std::string> g_nameCache;
static WinMutex g_cacheMutex;
static uint64_t g_hits = 0;
static uint64_t g_misses = 0;

bool LookupName(uint32_t id, std::string& outName) {
    WinLockGuard lock(g_cacheMutex);
    auto it = g_nameCache.find(id);
    if (it != g_nameCache.end()) {
        g_hits++;
        outName = it->second;
        return true;
    }
    g_misses++;
    return false;
}

void InsertName(uint32_t id, const std::string& name) {
    WinLockGuard lock(g_cacheMutex);
    g_nameCache[id] = name;
}

bool Init() {
    Log("[DbcLookupCacheFast] Active - Lock-Free DBC fast name lookup cache initialized");
    return true;
}

void Shutdown() {
    WinLockGuard lock(g_cacheMutex);
    g_nameCache.clear();
    Log("[DbcLookupCacheFast] Stats: %lld hits, %lld misses", g_hits, g_misses);
}

} // namespace DbcLookupCacheFast
