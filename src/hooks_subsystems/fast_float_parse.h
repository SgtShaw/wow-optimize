#pragma once
#include <windows.h>

namespace FastFloatParse {
    bool Init();
    void Shutdown();
    float FastAtof(const char* str);
}
