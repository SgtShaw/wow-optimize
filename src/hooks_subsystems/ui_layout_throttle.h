#pragma once
#include <windows.h>

namespace UILayoutThrottle {
    bool Init();
    void Shutdown();
    bool ShouldThrottle(void* frame);
    void ResetFrameCounter();

    // Feature 34 (0x00865270): Throttled UI Layout Recalculation
    bool OptimizeSub865270_UILayoutThrottle(void* frame, DWORD currentTick);
}
