#include "spell_overlay_preload.h"
#include <unordered_set>
#include "win_mutex.h"

namespace SpellOverlayPreload {

static std::unordered_set<std::string> g_preloadedOverlays;
static WinMutex g_overlayMutex;
static uint64_t g_hits = 0;

bool Init() {
    return true;
}

void Shutdown() {
    WinLockGuard lock(g_overlayMutex);
    g_preloadedOverlays.clear();
}

void PreloadOverlay(const std::string& texturePath) {
    if (texturePath.empty()) return;
    WinLockGuard lock(g_overlayMutex);
    if (g_preloadedOverlays.count(texturePath) == 0) {
        g_preloadedOverlays.insert(texturePath);
        g_hits++;
    }
}

} // namespace SpellOverlayPreload
