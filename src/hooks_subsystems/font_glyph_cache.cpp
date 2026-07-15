#include <windows.h>
#include <unordered_map>
#include "win_mutex.h"
#include "MinHook.h"
#include "version.h"
#include "font_glyph_cache.h"

extern "C" void Log(const char* fmt, ...);

static inline bool IsTeardownState() {
    uintptr_t gL = *(uintptr_t*)0x00D3F78C;
    return (gL < 0x10000 || gL > 0xFFE00000);
}

namespace FontGlyphCache {

struct GlyphKey {
    void* fontObj;
    unsigned int charCode;
    int fontSize;
    int a4;
    int a6;
    int a7;

    bool operator==(const GlyphKey& o) const {
        return fontObj == o.fontObj && charCode == o.charCode && fontSize == o.fontSize && a4 == o.a4 && a6 == o.a6 && a7 == o.a7;
    }
};

struct GlyphKeyHash {
    size_t operator()(const GlyphKey& k) const {
        size_t h = std::hash<void*>{}(k.fontObj);
        h ^= std::hash<unsigned int>{}(k.charCode) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.fontSize) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.a4) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.a6) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.a7) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct GlyphCacheEntry {
    uint8_t  structBytes[40];
    uint32_t textureSize;
    uint8_t* textureCopy;
};

static std::unordered_map<GlyphKey, GlyphCacheEntry, GlyphKeyHash> g_glyphCache;
static WinMutex g_cacheMutex;
static uint64_t g_hits = 0;
static uint64_t g_misses = 0;

// GxuAlloc type definition (sub_76E540 debug allocator)
typedef void* (__stdcall* fn_GxuAlloc_t)(int size, const char* file, int line, int flags);
static const fn_GxuAlloc_t fn_GxuAlloc = (fn_GxuAlloc_t)0x0076E540;

// sub_6C8CC0 detour hook typedef
typedef char (__cdecl* fn_GxuLoadGlyph_t)(void* fontObj, unsigned int charCode, int fontSize, int a4, void* outStruct, int a6, int a7);
static fn_GxuLoadGlyph_t orig_GxuLoadGlyph = nullptr;

static char __cdecl Hooked_GxuLoadGlyph(void* fontObj, unsigned int charCode, int fontSize, int a4, void* outStruct, int a6, int a7) {
    if (fontObj && charCode && outStruct && !IsTeardownState()) {
        GlyphKey key = { fontObj, charCode, fontSize, a4, a6, a7 };
        
        // Fast thread-safe cache lookup
        {
            WinLockGuard lock(g_cacheMutex);
            auto it = g_glyphCache.find(key);
            if (it != g_glyphCache.end()) {
                g_hits++;
                // Copy the 40 bytes of the cached glyph descriptor structure
                memcpy(outStruct, it->second.structBytes, 40);
                
                // Allocate a fresh texture buffer copy using the engine's CRT allocation wrapper
                uint32_t size = it->second.textureSize;
                void* newBuf = fn_GxuAlloc(size, ".\\IGxuFontGlyph.cpp", 0x93, 0);
                if (newBuf) {
                    memcpy(newBuf, it->second.textureCopy, size);
                    *reinterpret_cast<void**>(outStruct) = newBuf; // set pointer at offset 0
                    return 1; // success
                }
            }
        }
    }

    // Cache miss: compile/rasterize glyph outline normally
    char result = orig_GxuLoadGlyph(fontObj, charCode, fontSize, a4, outStruct, a6, a7);
    if (result && fontObj && charCode && outStruct && !IsTeardownState()) {
        GlyphKey key = { fontObj, charCode, fontSize, a4, a6, a7 };
        
        void* textureData = *reinterpret_cast<void**>(outStruct);
        uint32_t textureSize = *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(outStruct) + 4);
        
        if (textureData && textureSize > 0 && textureSize < 1048576) { // sanity bound: size < 1MB
            WinLockGuard lock(g_cacheMutex);
            g_misses++;
            
            // Allocate a local buffer copy of the texture data for our cache
            uint8_t* cacheBuf = reinterpret_cast<uint8_t*>(malloc(textureSize));
            if (cacheBuf) {
                memcpy(cacheBuf, textureData, textureSize);
                
                GlyphCacheEntry entry;
                memcpy(entry.structBytes, outStruct, 40);
                entry.textureSize = textureSize;
                entry.textureCopy = cacheBuf;
                
                // Insert into cache, evicting any previous entry under this key
                auto oldIt = g_glyphCache.find(key);
                if (oldIt != g_glyphCache.end()) {
                    if (oldIt->second.textureCopy) free(oldIt->second.textureCopy);
                }
                g_glyphCache[key] = entry;
            }
        }
    }
    return result;
}

bool Init() {
    g_hits = 0;
    g_misses = 0;

    if (WineSafe_CreateHook((void*)0x006C8CC0, (void*)Hooked_GxuLoadGlyph, (void**)&orig_GxuLoadGlyph) == MH_OK) {
        if (MH_EnableHook((void*)0x006C8CC0) == MH_OK) {
            Log("[FontGlyphCache] Hooked GxuLoadGlyph at 0x006C8CC0 successfully");
        } else {
            Log("[FontGlyphCache] Failed to enable hook on GxuLoadGlyph");
            return false;
        }
    } else {
        Log("[FontGlyphCache] Failed to create hook on GxuLoadGlyph");
        return false;
    }

    Log("[FontGlyphCache] Active - Font Glyph Pre-Caching Atlas Initialized");
    return true;
}

void Shutdown() {
    MH_DisableHook((void*)0x006C8CC0);
    
    WinLockGuard lock(g_cacheMutex);
    for (auto& pair : g_glyphCache) {
        if (pair.second.textureCopy) {
            free(pair.second.textureCopy);
        }
    }
    g_glyphCache.clear();
    
    Log("[FontGlyphCache] Stats: %lld hits, %lld misses (%.1f%% hit rate)", 
        g_hits, g_misses, (g_hits + g_misses) ? (100.0 * g_hits / (g_hits + g_misses)) : 0.0);
}

} // namespace FontGlyphCache
