#pragma once
#include <windows.h>
#include <string>

namespace CombatLogAsync {
    bool Init();
    void Shutdown();
    void WriteLogAsync(const std::string& line);
}
