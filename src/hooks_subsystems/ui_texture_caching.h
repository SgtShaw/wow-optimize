#pragma once
#include <windows.h>

namespace UiTextureCaching {
    bool Init();
    void Shutdown();
    void* GetCachedTexture(const char* filename);
    void AddToCache(const char* filename, void* texture);
}
