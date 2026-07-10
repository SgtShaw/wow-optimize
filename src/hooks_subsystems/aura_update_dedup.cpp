#include "aura_update_dedup.h"
#include <mutex>
#include <unordered_map>

namespace AuraUpdateDedup {
    struct AuraState {
        unsigned __int64 guid;
        unsigned int spellId;
        bool applied;
        DWORD timestamp;
    };

    static constexpr int HASH_SIZE = 1024;
    static AuraState g_states[HASH_SIZE] = {};
    static std::mutex g_stateMutex;
    static bool g_enabled = true;

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    bool ShouldSkipAuraUpdate(unsigned __int64 unitGuid, unsigned int spellId, bool isApplied) {
        if (!g_enabled) return false;

        size_t hash = ((size_t)unitGuid ^ (size_t)spellId) % HASH_SIZE;
        DWORD now = GetTickCount();

        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (g_states[hash].guid == unitGuid && g_states[hash].spellId == spellId && g_states[hash].applied == isApplied) {
            if (now - g_states[hash].timestamp < 100) { // Coalesce identical updates within 100ms
                return true; 
            }
        }

        g_states[hash].guid = unitGuid;
        g_states[hash].spellId = spellId;
        g_states[hash].applied = isApplied;
        g_states[hash].timestamp = now;
        return false;
    }
}
