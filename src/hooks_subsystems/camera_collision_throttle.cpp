#include "camera_collision_throttle.h"
#include <atomic>

namespace CameraCollisionThrottle {
    static bool g_enabled = true;
    static std::atomic<unsigned int> g_frameCount{0};

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    bool ShouldSkipCollisionCheck() {
        if (!g_enabled) return false;
        
        // Skip check on alternate frames (running it at 50% frequency)
        unsigned int frame = g_frameCount.fetch_add(1, std::memory_order_relaxed);
        return (frame % 2 != 0);
    }

    // Feature 23 (0x00821A20): Throttled Camera Collision Raycast
    bool OptimizeSub821A20_CameraRayThrottle(float camX, float camY, float camZ) {
        static float lastX = 0, lastY = 0, lastZ = 0;
        if (lastX == camX && lastY == camY && lastZ == camZ) {
            return true; // Position unchanged, skip raycast
        }
        lastX = camX; lastY = camY; lastZ = camZ;
        return false;
    }
}
