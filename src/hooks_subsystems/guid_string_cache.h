#pragma once
#include <windows.h>

namespace GuidStringCache {
    bool Init();
    void Shutdown();
    const char* GetGuidString(unsigned __int64 guid);
}
