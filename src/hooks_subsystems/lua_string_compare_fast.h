#pragma once
#include <windows.h>

namespace LuaStringCompareFast {
    bool Init();
    void Shutdown();
    int CompareStringsSse(const char* s1, const char* s2, size_t len);
}
