#pragma once
#include <windows.h>

namespace ItemDataPrefetch {
    bool Init();
    void Shutdown();
    void PrefetchItem(unsigned int itemId);
}
