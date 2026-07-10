#pragma once
#include <windows.h>

namespace MouseCursorSmooth {
    bool Init();
    void Shutdown();
    void OnFrame();
}
