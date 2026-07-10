#pragma once
#include <windows.h>

namespace DynamicShadowScaler {
    bool Init();
    void Shutdown();
    void OnFrame(float elapsedMs);
}
