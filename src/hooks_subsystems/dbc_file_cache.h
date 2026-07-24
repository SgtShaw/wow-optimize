#pragma once
#include <windows.h>
#include <cstdint>

namespace DbcFileCache {
    bool Init();
    void Shutdown();
    void* LookupRecord(void* dbc, uint32_t id);

    // Feature 47 (0x006337D0): Asynchronous DBC Preloader
    void OptimizeSub6337D0_AsyncDBC(const char* dbcFileName);
}
