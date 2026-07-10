#pragma once
#include <windows.h>

namespace DbcRowCaching {
    bool Init();
    void Shutdown();
    bool GetCachedRow(void* dbc, int rowId, void*& outRow);
    void AddToCache(void* dbc, int rowId, void* row);
}
