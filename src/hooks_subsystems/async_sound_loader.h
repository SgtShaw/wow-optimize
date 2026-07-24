#pragma once
#include <string>

namespace AsyncSoundLoader {
    bool Init();
    void Shutdown();
    void PreloadSound(const std::string& filePath);

    // Feature 10 (0x0080E1B0): Asynchronous Sound I/O Task
    void OptimizeSub80E1B0_AsyncSound(const char* soundPath);
}
