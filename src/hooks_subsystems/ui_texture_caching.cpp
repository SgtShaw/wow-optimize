#include "ui_texture_caching.h"
#include <unordered_map>
#include <string>
#include <mutex>

namespace UiTextureCaching {
    static bool g_enabled = true;
    static std::unordered_map<std::string, void*> g_uiTextureMap;
    static std::mutex g_uiTextureMutex;

    bool Init() {
        return true;
    }

    void Shutdown() {
        std::lock_guard<std::mutex> lock(g_uiTextureMutex);
        g_uiTextureMap.clear();
    }

    void* GetCachedTexture(const char* filename) {
        if (!g_enabled || !filename) return nullptr;

        std::lock_guard<std::mutex> lock(g_uiTextureMutex);
        auto it = g_uiTextureMap.find(filename);
        if (it != g_uiTextureMap.end()) {
            return it->second;
        }
        return nullptr;
    }

    void AddToCache(const char* filename, void* texture) {
        if (!g_enabled || !filename || !texture) return;

        std::lock_guard<std::mutex> lock(g_uiTextureMutex);
        g_uiTextureMap[filename] = texture;
    }
}
