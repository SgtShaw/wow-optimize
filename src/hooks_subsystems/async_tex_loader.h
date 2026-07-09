#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace AsyncTexLoader {
    bool Init();
    void Shutdown();
    void OnFrame();
    bool GetCachedTextureData(const std::string& path, std::vector<uint8_t>& outData);
}
