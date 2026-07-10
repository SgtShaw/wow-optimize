#pragma once
#include <windows.h>

namespace CombatLogFilter {
    bool Init();
    void Shutdown();
    bool ShouldFilterEvent(int eventId, const char* format, va_list args);
}
