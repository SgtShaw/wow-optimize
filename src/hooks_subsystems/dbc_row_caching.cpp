#include "dbc_row_caching.h"
#include <mutex>

namespace DbcRowCaching {
    struct CacheEntry {
        void* dbc;
        int rowId;
        void* row;
        bool valid;
    };

    static constexpr int CACHE_SIZE = 512;
    static CacheEntry g_rowCache[CACHE_SIZE] = {};
    static std::mutex g_cacheMutex;
    static bool g_enabled = true;

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    bool GetCachedRow(void* dbc, int rowId, void*& outRow) {
        if (!g_enabled || !dbc) return false;

        // Hash by combining DBC pointer and row ID
        size_t hash = ((size_t)dbc ^ (size_t)rowId) % CACHE_SIZE;
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        if (g_rowCache[hash].valid && g_rowCache[hash].dbc == dbc && g_rowCache[hash].rowId == rowId) {
            outRow = g_rowCache[hash].row;
            return true;
        }
        return false;
    }

    void AddToCache(void* dbc, int rowId, void* row) {
        if (!g_enabled || !dbc || !row) return;

        size_t hash = ((size_t)dbc ^ (size_t)rowId) % CACHE_SIZE;
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_rowCache[hash].dbc = dbc;
        g_rowCache[hash].rowId = rowId;
        g_rowCache[hash].row = row;
        g_rowCache[hash].valid = true;
    }
}
