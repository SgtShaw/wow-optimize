#pragma once
#include <windows.h>
#include <cstdint>

namespace CDataStoreBuffering {
    bool Init();
    void Shutdown();
    void* GetBufferedData(void* dataStore, uint32_t offset);
}
