#pragma once
#include <windows.h>
#include <string>

namespace SavedVarsBackup {
    bool Init();
    void Shutdown();
    void CreateBackup(const std::string& filePath);
}
