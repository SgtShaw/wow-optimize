#pragma once
#include <windows.h>

namespace TerrainHeightCache {
    bool Init();
    void Shutdown();
    bool GetCachedHeight(float x, float y, float& outZ);
    void AddToCache(float x, float y, float z);
    void Clear();
}
