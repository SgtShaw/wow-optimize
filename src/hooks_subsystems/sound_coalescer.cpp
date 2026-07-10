#include "sound_coalescer.h"
#include "sound_volume_limit.h"
#include "MinHook.h"
#include "version.h"
#include <unordered_map>
#include <mutex>

extern "C" void Log(const char* fmt, ...);

namespace SoundCoalescer {

typedef int (APIENTRY *FSOUND_PlaySound_fn)(int channel, void* sptr);
static FSOUND_PlaySound_fn orig_FSOUND_PlaySound = nullptr;

typedef int (APIENTRY *FSOUND_PlaySoundEx_fn)(int channel, void* sptr, void* dsp, signed char startpaused);
static FSOUND_PlaySoundEx_fn orig_FSOUND_PlaySoundEx = nullptr;

static std::unordered_map<void*, DWORD> g_playTimes;
static std::mutex g_soundMutex;
static uint64_t g_coalescedPlays = 0;

static int APIENTRY Hooked_FSOUND_PlaySound(int channel, void* sptr) {
    if (sptr) {
        DWORD now = GetTickCount();
        std::lock_guard<std::mutex> lock(g_soundMutex);
        auto it = g_playTimes.find(sptr);
        if (it != g_playTimes.end() && (now - it->second < 30)) {
            g_coalescedPlays++;
            return 99; // Return a dummy channel index to skip actual play
        }
        g_playTimes[sptr] = now;
    }
    int res = orig_FSOUND_PlaySound(channel, sptr);
    if (res >= 0) {
        ::SoundVolumeLimit::LimitChannelVolume(res, sptr);
    }
    return res;
}

static int APIENTRY Hooked_FSOUND_PlaySoundEx(int channel, void* sptr, void* dsp, signed char startpaused) {
    if (sptr) {
        DWORD now = GetTickCount();
        std::lock_guard<std::mutex> lock(g_soundMutex);
        auto it = g_playTimes.find(sptr);
        if (it != g_playTimes.end() && (now - it->second < 30)) {
            g_coalescedPlays++;
            return 99; // Dummy channel
        }
        g_playTimes[sptr] = now;
    }
    int res = orig_FSOUND_PlaySoundEx(channel, sptr, dsp, startpaused);
    if (res >= 0) {
        ::SoundVolumeLimit::LimitChannelVolume(res, sptr);
    }
    return res;
}

bool Init() {
    HMODULE hFmod = GetModuleHandleA("fmod.dll");
    if (!hFmod) {
        Log("[SoundCoalescer] fmod.dll not loaded yet");
        return false;
    }

    void* pPlay = (void*)GetProcAddress(hFmod, "FSOUND_PlaySound");
    void* pPlayEx = (void*)GetProcAddress(hFmod, "FSOUND_PlaySoundEx");

    if (pPlay && MH_CreateHook(pPlay, (void*)Hooked_FSOUND_PlaySound, (void**)&orig_FSOUND_PlaySound) == MH_OK) {
        MH_EnableHook(pPlay);
        Log("[SoundCoalescer] Hooked FSOUND_PlaySound successfully");
    }

    if (pPlayEx && MH_CreateHook(pPlayEx, (void*)Hooked_FSOUND_PlaySoundEx, (void**)&orig_FSOUND_PlaySoundEx) == MH_OK) {
        MH_EnableHook(pPlayEx);
        Log("[SoundCoalescer] Hooked FSOUND_PlaySoundEx successfully");
    }

    Log("[SoundCoalescer] Active - Advanced Sound Channels Coalescer initialized");
    return true;
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_soundMutex);
    g_playTimes.clear();
    Log("[SoundCoalescer] Stats: Coalesced %lld sound plays", g_coalescedPlays);
}

} // namespace SoundCoalescer
