#pragma once
#include <windows.h>

namespace NameplateCulling {
    bool Init();
    void Shutdown();
    bool ShouldCullNameplate(void* unit, float distance, bool visible);
}
