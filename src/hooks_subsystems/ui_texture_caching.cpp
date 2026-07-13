#include "ui_texture_caching.h"
#include <unordered_map>
#include <string>
#include "win_mutex.h"

namespace UiTextureCaching {
    static bool g_enabled = true;
    static std::unordered_map<std::string, void*> g_uiTextureMap;
    static WinMutex g_uiTextureMutex;

    bool Init() {
        return true;
    }

    void Shutdown() {
        WinLockGuard lock(g_uiTextureMutex);
        g_uiTextureMap.clear();
    }

    void* GetCachedTexture(const char* filename) {
        if (!g_enabled || !filename) return nullptr;

        WinLockGuard lock(g_uiTextureMutex);
        auto it = g_uiTextureMap.find(filename);
        if (it != g_uiTextureMap.end()) {
            return it->second;
        }
        return nullptr;
    }

    void AddToCache(const char* filename, void* texture) {
        if (!g_enabled || !filename || !texture) return;

        WinLockGuard lock(g_uiTextureMutex);
        g_uiTextureMap[filename] = texture;
    }
}
