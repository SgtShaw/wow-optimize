#pragma once
#include <windows.h>

namespace CameraShakeOpt {
    bool Init();
    void Shutdown();
    void FilterCameraShake(float* offsetVector);
}
