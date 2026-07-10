#pragma once
#include <windows.h>

namespace SoundFreqCoalesce {
    bool Init();
    void Shutdown();
    bool ShouldCoalesceFrequency(float frequency);
}
