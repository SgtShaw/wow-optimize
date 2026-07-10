#pragma once
#include <windows.h>
#include <string>

namespace CombatTextFont {
    bool Init();
    void Shutdown();
    void* LookupCombatFont(const std::string& fontName, int size);
}
