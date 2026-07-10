#include "spell_effect_culling.h"
#include <atomic>

namespace SpellEffectCulling {
    static bool g_enabled = true;
    static std::atomic<int> g_activeParticles{0};
    static constexpr int MAX_PARTICLES_THRESHOLD = 5000;

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    bool ShouldCullEffect(unsigned int effectId) {
        if (!g_enabled) return false;

        int current = g_activeParticles.load(std::memory_order_relaxed);
        if (current > MAX_PARTICLES_THRESHOLD) {
            // Cull 50% of the minor visual effects when above limit
            if (effectId % 2 == 0) {
                return true;
            }
        }
        return false;
    }
}
