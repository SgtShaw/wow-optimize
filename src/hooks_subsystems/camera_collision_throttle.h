#pragma once
#include <windows.h>

namespace CameraCollisionThrottle {
    bool Init();
    void Shutdown();
    bool ShouldSkipCollisionCheck();

    // Feature 23 (0x00821A20): Throttled Camera Collision Raycast
    bool OptimizeSub821A20_CameraRayThrottle(float camX, float camY, float camZ);
}
