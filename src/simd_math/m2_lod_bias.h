#pragma once
#include <windows.h>

namespace M2LodBias {
    bool Init();
    void Shutdown();
    void UpdateLodBias(double frameMs);
}
