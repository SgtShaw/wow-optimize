#pragma once
#include <windows.h>

namespace FontAlphaFastpath {
    bool Init();
    void Shutdown();
    bool ShouldApplyFastpath(unsigned int color);
}
