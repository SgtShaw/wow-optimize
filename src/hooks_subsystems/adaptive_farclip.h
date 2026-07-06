#pragma once
#include <windows.h>

namespace AdaptiveFarclip {
    bool Init();
    void Shutdown();
    void OnFrame(float elapsedMs);
}
