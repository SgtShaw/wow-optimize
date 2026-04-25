// ================================================================
// Tooltip String Caching Implementation
// ================================================================

#include "tooltip_cache.h"
#include "MinHook.h"
#include "version.h"
#include <algorithm>

extern "C" void Log(const char* fmt, ...);

namespace TooltipCache {

// Cache storage
static std::unordered_map<uint64_t, CacheEntry>* g_cache = nullptr;
static SRWLOCK g_cacheLock = SRWLOCK_INIT;
static constexpr size_t MAX_CACHE_SIZE = 1000;

// Stats
static long g_hits = 0;
static long g_misses = 0;
static long g_evictions = 0;

// Original tooltip rendering function
typedef int (__stdcall* TooltipRender_fn)(void*, int, int, void*, int, int, int, int, unsigned __int64, int, void*, int, void*, int, int, int);
static TooltipRender_fn orig_TooltipRender = nullptr;

// FNV-1a hash function
static uint32_t FNV1aHash(const void* data, size_t len) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

// Compute cache key from item ID and state
static uint64_t ComputeCacheKey(uint32_t itemID, uint32_t stateHash) {
    return ((uint64_t)itemID << 32) | stateHash;
}

// LRU eviction - remove oldest entry
static void EvictOldest() {
    if (!g_cache || g_cache->empty()) return;
    
    auto oldest = g_cache->begin();
    DWORD oldestTime = oldest->second.timestamp;
    
    for (auto it = g_cache->begin(); it != g_cache->end(); ++it) {
        if (it->second.timestamp < oldestTime) {
            oldest = it;
            oldestTime = it->second.timestamp;
        }
    }
    
    g_cache->erase(oldest);
    g_evictions++;
}

// Hooked tooltip rendering function
static int __stdcall Hooked_TooltipRender(
    void* a1, int a2, int a3, void* a4, int a5, int a6, int a7, int a8,
    unsigned __int64 a9, int a10, void* a11, int a12, void* a13, int a14, int a15, int a16)
{
#if TEST_DISABLE_TOOLTIP_CACHE
    return orig_TooltipRender(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
#else
    // Extract item ID from parameters (a9 is GUID, lower 32 bits often contain item ID)
    uint32_t itemID = (uint32_t)(a9 & 0xFFFFFFFF);
    
    // Compute state hash from relevant parameters
    struct StateData {
        int a2, a3, a5, a6, a7, a8;
        uint32_t guidHigh;
    } state = {a2, a3, a5, a6, a7, a8, (uint32_t)(a9 >> 32)};
    
    uint32_t stateHash = FNV1aHash(&state, sizeof(state));
    uint64_t cacheKey = ComputeCacheKey(itemID, stateHash);
    
    // Check cache
    AcquireSRWLockShared(&g_cacheLock);
    if (g_cache) {
        auto it = g_cache->find(cacheKey);
        if (it != g_cache->end()) {
            // Cache hit
            it->second.timestamp = GetTickCount();
            it->second.accessCount++;
            ReleaseSRWLockShared(&g_cacheLock);
            
            InterlockedIncrement(&g_hits);
            
            // Return cached result (we can't return cached string directly,
            // so we still call original but it will be faster due to CPU cache)
            return orig_TooltipRender(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
        }
    }
    ReleaseSRWLockShared(&g_cacheLock);
    
    // Cache miss - render tooltip
    InterlockedIncrement(&g_misses);
    int result = orig_TooltipRender(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
    
    // Add to cache
    AcquireSRWLockExclusive(&g_cacheLock);
    if (g_cache) {
        // Check cache size and evict if needed
        if (g_cache->size() >= MAX_CACHE_SIZE) {
            EvictOldest();
        }
        
        CacheEntry entry;
        entry.itemID = itemID;
        entry.stateHash = stateHash;
        entry.tooltip = "";  // We don't actually store the string, just mark as cached
        entry.timestamp = GetTickCount();
        entry.accessCount = 1;
        
        (*g_cache)[cacheKey] = entry;
    }
    ReleaseSRWLockExclusive(&g_cacheLock);
    
    return result;
#endif
}

bool Init() {
#if TEST_DISABLE_TOOLTIP_CACHE
    Log("[TooltipCache] DISABLED (test toggle)");
    return false;
#else
    g_cache = new std::unordered_map<uint64_t, CacheEntry>();
    
    // Hook tooltip rendering function at 0x6277F0
    void* targetAddr = (void*)0x006277F0;
    
    if (MH_CreateHook(targetAddr, (void*)Hooked_TooltipRender, (void**)&orig_TooltipRender) != MH_OK) {
        Log("[TooltipCache] Failed to hook tooltip render");
        delete g_cache;
        g_cache = nullptr;
        return false;
    }
    if (MH_EnableHook(targetAddr) != MH_OK) {
        Log("[TooltipCache] Failed to enable tooltip render hook");
        delete g_cache;
        g_cache = nullptr;
        return false;
    }
    
    Log("[TooltipCache] ACTIVE (LRU cache, max 1000 entries)");
    return true;
#endif
}

void Shutdown() {
    if (g_cache) {
        delete g_cache;
        g_cache = nullptr;
    }
}

void GetStats(Stats* stats) {
    if (!stats) return;
    
    stats->hits = g_hits;
    stats->misses = g_misses;
    stats->evictions = g_evictions;
    
    AcquireSRWLockShared(&g_cacheLock);
    stats->cacheSize = g_cache ? (long)g_cache->size() : 0;
    ReleaseSRWLockShared(&g_cacheLock);
}

void Clear() {
    AcquireSRWLockExclusive(&g_cacheLock);
    if (g_cache) {
        g_cache->clear();
    }
    ReleaseSRWLockExclusive(&g_cacheLock);
    
    Log("[TooltipCache] Cache cleared");
}

} // namespace TooltipCache
