#pragma once
#include <windows.h>

namespace FrameScriptMemOpt {
    bool Init();
    void Shutdown();
    void* AllocateScriptBlock(size_t size);
    void FreeScriptBlock(void* ptr);
}
