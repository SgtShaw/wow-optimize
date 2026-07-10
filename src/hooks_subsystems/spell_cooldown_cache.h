#pragma once
#include <windows.h>

namespace SpellCooldownCache {
    bool Init();
    void Shutdown();
    bool GetCachedCooldown(unsigned int spellId, float& outStart, float& outDuration);
    void AddToCache(unsigned int spellId, float start, float duration);
}
