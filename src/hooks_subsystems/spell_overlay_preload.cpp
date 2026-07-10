#include "spell_overlay_preload.h"
#include <unordered_set>
#include <mutex>

namespace SpellOverlayPreload {

static std::unordered_set<std::string> g_preloadedOverlays;
static std::mutex g_overlayMutex;
static uint64_t g_hits = 0;

bool Init() {
    return true;
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_overlayMutex);
    g_preloadedOverlays.clear();
}

void PreloadOverlay(const std::string& texturePath) {
    if (texturePath.empty()) return;
    std::lock_guard<std::mutex> lock(g_overlayMutex);
    if (g_preloadedOverlays.count(texturePath) == 0) {
        g_preloadedOverlays.insert(texturePath);
        g_hits++;
    }
}

} // namespace SpellOverlayPreload
