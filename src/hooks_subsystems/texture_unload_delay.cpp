#include "texture_unload_delay.h"
#include <unordered_map>
#include <string>
#include <mutex>

namespace TextureUnloadDelay {
    static bool g_enabled = true;
    static std::mutex g_textureLock;
    static std::unordered_map<std::string, DWORD> g_recentTextures;

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    bool ShouldDelayUnload(const char* texturePath) {
        if (!g_enabled || !texturePath) return false;

        std::string path(texturePath);
        DWORD now = GetTickCount();
        std::lock_guard<std::mutex> lock(g_textureLock);
        
        // Cache that this texture was requested for unloading
        auto it = g_recentTextures.find(path);
        if (it == g_recentTextures.end()) {
            g_recentTextures[path] = now;
            return true; // Delay it the first time it is queried for unloading within 5 seconds
        } else {
            if (now - it->second < 5000) { // Keep it cached for 5 seconds
                return true;
            } else {
                g_recentTextures.erase(it); // Ready to be unloaded now
                return false;
            }
        }
    }
}
