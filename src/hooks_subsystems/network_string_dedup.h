#pragma once
#include <windows.h>

namespace NetworkStringDedup {
    bool Init();
    void Shutdown();
    const char* GetDedupedString(const char* original);
}
