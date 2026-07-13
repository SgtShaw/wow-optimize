#include "sound_freq_coalesce.h"
#include "win_mutex.h"
#include <unordered_map>

namespace SoundFreqCoalesce {
    static bool g_enabled = true;
    static WinMutex g_soundMutex;
    static std::unordered_map<int, DWORD> g_lastPlayedFreq;

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    bool ShouldCoalesceFrequency(float frequency) {
        if (!g_enabled) return false;

        int freqKey = (int)(frequency * 100.0f); // Map to int
        DWORD now = GetTickCount();
        WinLockGuard lock(g_soundMutex);
        auto it = g_lastPlayedFreq.find(freqKey);
        if (it != g_lastPlayedFreq.end()) {
            if (now - it->second < 50) { // Limit duplicate frequencies to once every 50ms
                return true; 
            }
        }
        g_lastPlayedFreq[freqKey] = now;
        return false;
    }
}
