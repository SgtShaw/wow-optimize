#pragma once
#include <windows.h>

namespace LuaGcGovernor {
    bool Init();
    void Shutdown();
    void OnFrame(float elapsedMs);
}
