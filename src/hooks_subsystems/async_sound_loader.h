#pragma once
#include <string>

namespace AsyncSoundLoader {
    bool Init();
    void Shutdown();
    void PreloadSound(const std::string& filePath);

    // Asynchronous Sound I/O Task
    void OptimizeSub80E1B0_AsyncSound(const char* soundPath);

    // Asynchronous Resource Buffer Loader
    void OptimizeSub7976A0_AsyncResourceBuffer(const char* resPath);
}
