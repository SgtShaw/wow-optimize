#include <windows.h>
#include <unordered_map>
#include <mutex>
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace FontGlyphCache {

struct GlyphData {
    int width;
    int height;
    int advance;
    void* textureData;
};

// Thread-safe atlas cache for font glyphs
static std::unordered_map<uint32_t, GlyphData> g_glyphCache;
static std::mutex g_cacheMutex;
static uint64_t g_hits = 0;
static uint64_t g_misses = 0;

bool GetGlyph(uint32_t charCode, GlyphData* outData) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    auto it = g_glyphCache.find(charCode);
    if (it != g_glyphCache.end()) {
        g_hits++;
        *outData = it->second;
        return true;
    }
    g_misses++;
    return false;
}

void InsertGlyph(uint32_t charCode, const GlyphData& data) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_glyphCache[charCode] = data;
}

bool Init() {
    Log("[FontGlyphCache] Active - Font Glyph Pre-Caching Atlas Initialized");
    return true;
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    for (auto& pair : g_glyphCache) {
        if (pair.second.textureData) {
            free(pair.second.textureData);
        }
    }
    g_glyphCache.clear();
    Log("[FontGlyphCache] Stats: %lld hits, %lld misses", g_hits, g_misses);
}

} // namespace FontGlyphCache
