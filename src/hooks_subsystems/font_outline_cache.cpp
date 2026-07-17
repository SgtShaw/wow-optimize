#include "font_outline_cache.h"
#include "version.h"
#include <unordered_map>
#include "win_mutex.h"

extern "C" void Log(const char* fmt, ...);

namespace FontOutlineCache {

struct OutlineKey {
    void* font;
    uint32_t charCode;
    int style;

    bool operator==(const OutlineKey& o) const {
        return font == o.font && charCode == o.charCode && style == o.style;
    }
};

struct OutlineKeyHash {
    size_t operator()(const OutlineKey& k) const {
        return (size_t)k.font ^ (size_t)k.charCode ^ (size_t)k.style;
    }
};

static std::unordered_map<OutlineKey, void*, OutlineKeyHash> g_outlineCache;
static WinMutex g_outlineMutex;
static uint64_t g_outlineHits = 0;
static uint64_t g_outlineMisses = 0;

bool Init() {
    Log("[FontOutlineCache] Active - Font Glyph Outline Cache initialized");
    return true;
}

void Shutdown() {
    WinLockGuard lock(g_outlineMutex);
    g_outlineCache.clear();
    Log("[FontOutlineCache] Stats: %lld hits, %lld misses", g_outlineHits, g_outlineMisses);
}

void ClearCache() {
    WinLockGuard lock(g_outlineMutex);
    g_outlineCache.clear();
    Log("[FontOutlineCache] Cache cleared (device reset)");
}

void* LookupOutline(void* font, uint32_t charCode, int style) {
    if (!font) return nullptr;
    OutlineKey key = {font, charCode, style};
    WinLockGuard lock(g_outlineMutex);
    auto it = g_outlineCache.find(key);
    if (it != g_outlineCache.end()) {
        g_outlineHits++;
        return it->second;
    }
    g_outlineMisses++;
    return nullptr;
}

} // namespace FontOutlineCache
