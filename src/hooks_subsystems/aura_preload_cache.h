#pragma once
#include <windows.h>
#include <string>

namespace AuraPreloadCache {
    bool Init();
    void Shutdown();
    void PreloadAuraTexture(const std::string& path);

    // Feature 40 (0x008DA550): Asynchronous Aura Data Prefetcher
    void OptimizeSub8DA550_AsyncAura(int spellId, float x, float y, float z);
}
