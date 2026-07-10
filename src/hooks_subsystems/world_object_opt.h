#pragma once
#include <windows.h>

namespace WorldObjectOpt {
    bool Init();
    void Shutdown();
    bool ShouldSkipRender(void* obj);
}
