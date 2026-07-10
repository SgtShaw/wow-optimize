#include "wmo_culling_opt.h"
#include <cmath>

namespace WmoCullingOpt {
    static bool g_enabled = true;
    static float g_cullRadiusSq = 350.0f * 350.0f; // 350 meters limit

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    bool ShouldCullWmoGroup(int groupId, const float* boundsMin, const float* boundsMax) {
        if (!g_enabled || !boundsMin || !boundsMax) return false;

        // Calculate center of the WMO group bounding box
        float cx = (boundsMin[0] + boundsMax[0]) * 0.5f;
        float cy = (boundsMin[1] + boundsMax[1]) * 0.5f;
        float cz = (boundsMin[2] + boundsMax[2]) * 0.5f;

        // Check if group is excessively far
        float distSq = cx * cx + cy * cy + cz * cz;
        if (distSq > g_cullRadiusSq) {
            return true;
        }
        return false;
    }
}
