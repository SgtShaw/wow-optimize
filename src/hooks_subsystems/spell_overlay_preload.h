#pragma once
#include <windows.h>
#include <string>

namespace SpellOverlayPreload {
    bool Init();
    void Shutdown();
    void PreloadOverlay(const std::string& texturePath);
}
