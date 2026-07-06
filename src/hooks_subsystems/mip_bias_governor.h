#pragma once
#include <windows.h>

namespace MipBiasGovernor {
    bool Init();
    void Shutdown();
    void UpdateMipBias(double frameMs);
    float GetCurrentBias();
}
