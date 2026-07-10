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
}
