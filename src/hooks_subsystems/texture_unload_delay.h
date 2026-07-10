#pragma once
#include <windows.h>

namespace TextureUnloadDelay {
    bool Init();
    void Shutdown();
    bool ShouldDelayUnload(const char* texturePath);
}
