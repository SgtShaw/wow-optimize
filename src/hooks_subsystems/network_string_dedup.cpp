#include "network_string_dedup.h"
#include <unordered_set>
#include <string>
#include <mutex>

namespace NetworkStringDedup {
    static bool g_enabled = true;
    static std::unordered_set<std::string> g_stringPool;
    static std::mutex g_poolMutex;

    bool Init() {
        return true;
    }

    void Shutdown() {
        std::lock_guard<std::mutex> lock(g_poolMutex);
        g_stringPool.clear();
    }

    const char* GetDedupedString(const char* original) {
        if (!g_enabled || !original) return original;

        std::lock_guard<std::mutex> lock(g_poolMutex);
        auto it = g_stringPool.find(original);
        if (it != g_stringPool.end()) {
            return it->c_str(); // Return matching pooled string pointer
        }

        // Add to pool if it is not excessively long
        if (strlen(original) < 256) {
            auto inserted = g_stringPool.insert(original);
            return inserted.first->c_str();
        }
        return original;
    }
}
