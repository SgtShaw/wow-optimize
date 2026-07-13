#include "sound_volume_limit.h"
#include <unordered_map>
#include "win_mutex.h"

namespace SoundVolumeLimit {
    typedef signed char (APIENTRY *FSOUND_SetVolume_fn)(int channel, int volume);
    static FSOUND_SetVolume_fn FSOUND_SetVolume_ = nullptr;

    typedef signed char (APIENTRY *FSOUND_IsPlaying_fn)(int channel);
    static FSOUND_IsPlaying_fn FSOUND_IsPlaying_ = nullptr;

    static std::unordered_map<int, void*> g_channelSounds;
    static WinMutex g_soundLimitMutex;
    static bool g_enabled = true;

    bool Init() {
        HMODULE hFmod = GetModuleHandleA("fmod.dll");
        if (!hFmod) return false;

        FSOUND_SetVolume_ = (FSOUND_SetVolume_fn)GetProcAddress(hFmod, "FSOUND_SetVolume");
        FSOUND_IsPlaying_ = (FSOUND_IsPlaying_fn)GetProcAddress(hFmod, "FSOUND_IsPlaying");

        return FSOUND_SetVolume_ != nullptr && FSOUND_IsPlaying_ != nullptr;
    }

    void Shutdown() {
        // No-op
    }

    void LimitChannelVolume(int channel, void* sptr) {
        if (!g_enabled || channel < 0 || !sptr || !FSOUND_SetVolume_ || !FSOUND_IsPlaying_) return;

        WinLockGuard lock(g_soundLimitMutex);
        
        // Update the sound playing on this channel
        g_channelSounds[channel] = sptr;

        // Clean up inactive channels and count how many channels are currently playing this exact sound
        int activeCount = 0;
        for (auto it = g_channelSounds.begin(); it != g_channelSounds.end(); ) {
            if (!FSOUND_IsPlaying_(it->first)) {
                it = g_channelSounds.erase(it);
            } else {
                if (it->second == sptr) {
                    activeCount++;
                }
                ++it;
            }
        }

        // If the same sound is already playing in other channels, reduce the volume to prevent clipping
        if (activeCount > 2) {
            // FMOD volume range is 0 to 255
            // Reduce volume: 3rd instance is 50%, 4th is 25%, etc.
            int volume = 255;
            if (activeCount == 3) volume = 128;
            else if (activeCount == 4) volume = 64;
            else volume = 32;

            FSOUND_SetVolume_(channel, volume);
        }
    }
}
