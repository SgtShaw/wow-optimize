#include "camera_shake_opt.h"

namespace CameraShakeOpt {

static float g_shakeScale = 0.2f; // Scale down camera shake intensity to 20% to prevent motion nausea and stutters

bool Init() {
    return true;
}

void Shutdown() {
    // No-op
}

void FilterCameraShake(float* offsetVector) {
    if (!offsetVector) return;
    
    // Scale down the camera shake translation vector offsets (X, Y, Z translation coordinates)
    offsetVector[0] *= g_shakeScale;
    offsetVector[1] *= g_shakeScale;
    offsetVector[2] *= g_shakeScale;
}

} // namespace CameraShakeOpt
