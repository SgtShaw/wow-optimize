#pragma once
#include <windows.h>

namespace SavedVarsOpt {
    bool Init();
    void Shutdown();
    bool OptimizeSerialization(const char* filepath);
}
