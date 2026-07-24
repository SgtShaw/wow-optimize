#pragma once
#include <windows.h>

namespace MinimapRefreshGovernor {
    bool Init();
    void Shutdown();
    bool ShouldSkipRefresh();

    // Feature 18 (0x00923D50): Throttled Minimap Blip Refresh Engine
    bool OptimizeSub923D50_MinimapThrottle(DWORD tickCount);
}
