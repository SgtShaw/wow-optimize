#include "combat_text_font.h"
#include <unordered_map>
#include <mutex>

namespace CombatTextFont {

struct FontKey {
    std::string name;
    int size;

    bool operator==(const FontKey& o) const {
        return name == o.name && size == o.size;
    }
};

struct FontKeyHash {
    size_t operator()(const FontKey& k) const {
        return std::hash<std::string>{}(k.name) ^ k.size;
    }
};

static std::unordered_map<FontKey, void*, FontKeyHash> g_fontCache;
static std::mutex g_fontMutex;
static uint64_t g_hits = 0;

bool Init() {
    return true;
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_fontMutex);
    g_fontCache.clear();
}

void* LookupCombatFont(const std::string& fontName, int size) {
    if (fontName.empty()) return nullptr;
    
    // Only cache combat log / floating text fonts (typically fonts containing 'damage' or 'combat' or default names like 'arial')
    if (fontName.find("DAMAGE") != std::string::npos || fontName.find("Friz") != std::string::npos) {
        FontKey key = { fontName, size };
        std::lock_guard<std::mutex> lock(g_fontMutex);
        auto it = g_fontCache.find(key);
        if (it != g_fontCache.end()) {
            g_hits++;
            return it->second;
        }
    }
    return nullptr;
}

} // namespace CombatTextFont
