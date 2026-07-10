#pragma once
#include <windows.h>

namespace AnimBlendCache {
    bool Init();
    void Shutdown();
    bool GetCachedMatrix(void* model, int boneIndex, float animTime, float* outMatrix);
    void AddToCache(void* model, int boneIndex, float animTime, const float* matrix);
    void Clear();
}
