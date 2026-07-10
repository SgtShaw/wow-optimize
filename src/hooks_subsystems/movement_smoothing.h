#pragma once
#include <windows.h>

namespace MovementSmoothing {
    bool Init();
    void Shutdown();
    void SmoothPosition(void* entity, float* x, float* y, float* z);
}
