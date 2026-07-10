#include "terrain_height_cache.h"
#include <cmath>
#include <mutex>

namespace TerrainHeightCache {
    struct CacheEntry {
        float x;
        float y;
        float z;
        bool valid;
    };

    static constexpr int CACHE_SIZE = 512;
    static CacheEntry g_cache[CACHE_SIZE];
    static std::mutex g_cacheMutex;
    static bool g_enabled = true;

    bool Init() {
        Clear();
        return true;
    }

    void Shutdown() {
        Clear();
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        for (int i = 0; i < CACHE_SIZE; ++i) {
            g_cache[i].valid = false;
        }
    }

    inline unsigned int GetHash(float x, float y) {
        int ix = static_cast<int>(x * 10.0f);
        int iy = static_cast<int>(y * 10.0f);
        return (unsigned int)((ix * 73856093) ^ (iy * 19349663)) % CACHE_SIZE;
    }

    bool GetCachedHeight(float x, float y, float& outZ) {
        if (!g_enabled) return false;

        std::lock_guard<std::mutex> lock(g_cacheMutex);
        unsigned int idx = GetHash(x, y);
        if (g_cache[idx].valid) {
            if (std::abs(g_cache[idx].x - x) < 0.05f && std::abs(g_cache[idx].y - y) < 0.05f) {
                outZ = g_cache[idx].z;
                return true;
            }
        }
        return false;
    }

    void AddToCache(float x, float y, float z) {
        if (!g_enabled) return;

        std::lock_guard<std::mutex> lock(g_cacheMutex);
        unsigned int idx = GetHash(x, y);
        g_cache[idx].x = x;
        g_cache[idx].y = y;
        g_cache[idx].z = z;
        g_cache[idx].valid = true;
    }
}
