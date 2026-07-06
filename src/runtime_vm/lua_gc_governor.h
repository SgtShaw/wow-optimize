#pragma once
#include <windows.h>

namespace LuaGCGovernor {
    bool Init();
    void Shutdown();
    void OnFrame(double frameMs);
}
