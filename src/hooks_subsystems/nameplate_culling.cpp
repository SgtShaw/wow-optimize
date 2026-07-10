#include "nameplate_culling.h"

namespace NameplateCulling {
    static bool g_enabled = true;
    static float g_maxDistance = 41.0f; // Limit to standard 3.3.5a nameplate range

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    bool ShouldCullNameplate(void* unit, float distance, bool visible) {
        if (!g_enabled) return false;
        if (!visible) return true; // Cull if already invisible
        if (distance > g_maxDistance) return true; // Cull if too far
        return false;
    }
}
