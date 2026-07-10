#pragma once
#include <windows.h>

namespace WmoCullingOpt {
    bool Init();
    void Shutdown();
    bool ShouldCullWmoGroup(int groupId, const float* boundsMin, const float* boundsMax);
}
