#pragma once
#include <windows.h>
#include <cstdint>
#include <string>

namespace FontOutlineCache {
    bool Init();
    void Shutdown();
    void* LookupOutline(void* font, uint32_t charCode, int style);
}
