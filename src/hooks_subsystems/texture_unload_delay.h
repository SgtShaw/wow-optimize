#pragma once
#include <windows.h>

namespace TextureUnloadDelay {
    bool Init();
    void Shutdown();
    void OnFrame();
}
