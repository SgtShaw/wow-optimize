#include "guid_string_cache.h"
#include <mutex>
#include <cstdio>

namespace GuidStringCache {
    struct CacheEntry {
        unsigned __int64 guid;
        char str[32];
        bool valid;
    };

    static constexpr int CACHE_SIZE = 512;
    static CacheEntry g_cache[CACHE_SIZE] = {};
    static std::mutex g_cacheMutex;
    static bool g_enabled = true;

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    const char* GetGuidString(unsigned __int64 guid) {
        if (!g_enabled) return nullptr;

        size_t index = (size_t)(guid % CACHE_SIZE);
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        if (g_cache[index].valid && g_cache[index].guid == guid) {
            return g_cache[index].str;
        }

        // Cache miss: generate hex string
        g_cache[index].guid = guid;
        sprintf_s(g_cache[index].str, sizeof(g_cache[index].str), "0x%016I64X", guid);
        g_cache[index].valid = true;
        return g_cache[index].str;
    }
}
