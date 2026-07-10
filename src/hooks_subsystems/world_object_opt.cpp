#include "world_object_opt.h"
#include <cmath>
#include <cstdint>

namespace WorldObjectOpt {

static float g_lastCamX = 0.0f;
static float g_lastCamY = 0.0f;
static float g_lastCamZ = 0.0f;
static float g_lastCamRot = 0.0f;
static bool g_camMoved = true;
static uint64_t g_skippedRenders = 0;

bool Init() {
    return true;
}

void Shutdown() {
    // No-op
}

bool ShouldSkipRender(void* obj) {
    if (!obj) return false;
    
    // Read camera position from WoW engine (global camera offsets)
    float* camPos = (float*)0x00B42E70; // Active camera coordinates base in WoW 3.3.5a
    if (camPos) {
        float x = camPos[0];
        float y = camPos[1];
        float z = camPos[2];
        float rot = camPos[3];

        if (std::abs(x - g_lastCamX) < 0.01f &&
            std::abs(y - g_lastCamY) < 0.01f &&
            std::abs(z - g_lastCamZ) < 0.01f &&
            std::abs(rot - g_lastCamRot) < 0.01f) {
            g_camMoved = false;
        } else {
            g_camMoved = true;
            g_lastCamX = x;
            g_lastCamY = y;
            g_lastCamZ = z;
            g_lastCamRot = rot;
        }
    }

    if (!g_camMoved) {
        // Skip rendering non-interactive scenery objects (at offset 0x24 is type/flags)
        uint32_t type = *(uint32_t*)((char*)obj + 0x24);
        if (type == 3) { // 3 = Scenery/decor
            g_skippedRenders++;
            return true;
        }
    }

    return false;
}

} // namespace WorldObjectOpt
