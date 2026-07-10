#pragma once
#include <windows.h>

namespace SoundVolumeLimit {
    bool Init();
    void Shutdown();
    void LimitChannelVolume(int channel, void* sptr);
}
