#pragma once
#include <windows.h>

namespace NetAddonCoalescer {
    bool Init();
    void Shutdown();
    void OnFrame();

    // Feature 50 (0x004B3F80): Pre-allocated Memory Pool for Network Packets
    float* OptimizeSub4B3F80_PreallocBuffer(float* inVec);
}
