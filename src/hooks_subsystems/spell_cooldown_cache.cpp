#include "spell_cooldown_cache.h"
#include "win_mutex.h"

namespace SpellCooldownCache {
    struct CooldownEntry {
        unsigned int spellId;
        float start;
        float duration;
        DWORD timestamp;
        bool valid;
    };

    static constexpr int CACHE_SIZE = 256;
    static CooldownEntry g_cache[CACHE_SIZE] = {};
    static WinMutex g_cacheMutex;
    static bool g_enabled = true;

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    bool GetCachedCooldown(unsigned int spellId, float& outStart, float& outDuration) {
        if (!g_enabled) return false;

        size_t index = spellId % CACHE_SIZE;
        DWORD now = GetTickCount();

        WinLockGuard lock(g_cacheMutex);
        if (g_cache[index].valid && g_cache[index].spellId == spellId) {
            if (now - g_cache[index].timestamp < 33) { // Coalesce checks within the same frame (33ms)
                outStart = g_cache[index].start;
                outDuration = g_cache[index].duration;
                return true;
            }
        }
        return false;
    }

    void AddToCache(unsigned int spellId, float start, float duration) {
        if (!g_enabled) return;

        size_t index = spellId % CACHE_SIZE;
        DWORD now = GetTickCount();

        WinLockGuard lock(g_cacheMutex);
        g_cache[index].spellId = spellId;
        g_cache[index].start = start;
        g_cache[index].duration = duration;
        g_cache[index].timestamp = now;
        g_cache[index].valid = true;
    }
}
