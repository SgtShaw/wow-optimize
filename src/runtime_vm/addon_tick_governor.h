#pragma once
#include <windows.h>

namespace AddonTickGovernor {
    bool ShouldThrottle(const char* source);
    bool Init();
    void Shutdown();
}
