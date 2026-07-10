#pragma once
#include <windows.h>

namespace CameraCollisionThrottle {
    bool Init();
    void Shutdown();
    bool ShouldSkipCollisionCheck();
}
