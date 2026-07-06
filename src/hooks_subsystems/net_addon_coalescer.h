#pragma once
#include <windows.h>

namespace NetAddonCoalescer {
    bool Init();
    void Shutdown();
    void OnFrame();
}
