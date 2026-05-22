#pragma once

// ================================================================
// Tooltip String Caching - Cache formatted tooltip strings
//
// ================================================================

#include <windows.h>
#include <unordered_map>
#include <string>

namespace TooltipCache {

// Cache entry
struct CacheEntry {
    uint32_t itemID;
    uint32_t stateHash;      // Hash of item state (enchants, gems, etc.)
    std::string tooltip;     // Cached tooltip string
    DWORD timestamp;         // Last access time (for LRU)
    uint32_t accessCount;    // Access counter
};

// Cache statistics
struct Stats {
    long hits;
    long misses;
    long evictions;
    long cacheSize;
};

// Initialize tooltip cache
bool Init();

// Shutdown and cleanup
void Shutdown();

// Get cache statistics
void GetStats(Stats* stats);

// Clear cache (called on UI reload)
void Clear();

} // namespace TooltipCache
