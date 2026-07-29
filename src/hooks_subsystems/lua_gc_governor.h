#pragma once
#include <windows.h>

namespace LuaGcGovernor {
    bool Init();
    void Shutdown();
    void LogStats();
    void OnFrame(float elapsedMs);
}
