#include "ui_layout_throttle.h"
#include <unordered_map>
#include <mutex>

extern "C" void Log(const char* fmt, ...);

namespace UILayoutThrottle {
    static std::unordered_map<void*, int> g_frameUpdateCounts;
    static std::mutex g_throttleMutex;
    static bool g_enabled = true;
    static unsigned int g_throttledCount = 0;

    bool Init() {
        return true;
    }

    void Shutdown() {
        std::lock_guard<std::mutex> lock(g_throttleMutex);
        g_frameUpdateCounts.clear();
    }

    bool ShouldThrottle(void* frame) {
        if (!g_enabled || !frame) return false;

        std::lock_guard<std::mutex> lock(g_throttleMutex);
        int count = ++g_frameUpdateCounts[frame];
        
        // If a frame updates its layout more than 200 times in a single game frame,
        // it is almost certainly stuck in a layout loop or updating excessively.
        if (count > 200) {
            if (count == 201) {
                // Log only once per frame per problematic frame to avoid spam
                Log("[UILayoutThrottle] Warning: Frame %p is updating its layout excessively (%d times). Throttling to prevent layout loop.", frame, count);
            }
            g_throttledCount++;
            return true;
        }

        return false;
    }

    void ResetFrameCounter() {
        std::lock_guard<std::mutex> lock(g_throttleMutex);
        g_frameUpdateCounts.clear();
    }
}
