#pragma once
#include <windows.h>

namespace CombatEventLimit {
    bool Init();
    void Shutdown();
    bool ShouldLimitEvent(const char* eventName);
}
