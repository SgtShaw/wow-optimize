#pragma once
#include <windows.h>

namespace UnitMaxPowerCache {
    bool Init();
    void Shutdown();
    int GetMaxPower(void* unitObj, int powerType);
    void SetMaxPower(void* unitObj, int powerType, int val);
}
