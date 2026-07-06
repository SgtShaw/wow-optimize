#pragma once
#include <windows.h>

namespace SpatialCulling {
    bool Init();
    void Shutdown();
    void OnFrame();
    float GetSpatialCullBias(void* model, float distance);
}
