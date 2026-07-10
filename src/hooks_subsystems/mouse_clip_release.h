#pragma once
#include <windows.h>

namespace MouseClipRelease {
    bool Init();
    void Shutdown();
    void OnFocusChange(bool hasFocus);
}
