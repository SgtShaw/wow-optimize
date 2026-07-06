#pragma once
#include <windows.h>

namespace PerfDiagnostics {
    bool Init();
    void Shutdown();
    void OnFrame(double elapsedMs);
}
