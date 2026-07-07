#pragma once
#include <stdint.h>

namespace RcuObjMgr {
    bool Init();
    void Shutdown();
    void OnFrame();
    void UpdateRcuArray(void* objMgr);
    void UpdateActiveRcuArray();
}
