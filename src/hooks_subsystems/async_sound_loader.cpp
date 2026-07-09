#include <windows.h>
#include "core/config.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <unordered_map>
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace AsyncSoundLoader {

struct SoundBuffer {
    std::vector<uint8_t> data;
    bool ready;
};

static std::unordered_map<std::string, SoundBuffer> g_soundCache;
static std::mutex g_soundMutex;
static std::vector<std::thread> g_workerThreads;
static bool g_active = false;
static uint64_t g_preloads = 0;

void SoundLoadWorker(std::string filePath) {
    HANDLE hFile = CreateFileA(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    DWORD size = GetFileSize(hFile, NULL);
    if (size > 0 && size < 4 * 1024 * 1024) { // Preload sound files under 4MB
        std::vector<uint8_t> buffer(size);
        DWORD readBytes = 0;
        if (ReadFile(hFile, buffer.data(), size, &readBytes, NULL)) {
            std::lock_guard<std::mutex> lock(g_soundMutex);
            g_soundCache[filePath] = { std::move(buffer), true };
        }
    }
    CloseHandle(hFile);
}

void PreloadSound(const std::string& filePath) {
    if (!Config::g_settings.OptAudioDecodeMt) return;
    std::lock_guard<std::mutex> lock(g_soundMutex);
    if (g_soundCache.find(filePath) != g_soundCache.end()) return;
    g_soundCache[filePath] = { {}, false };
    
    g_preloads++;
    g_workerThreads.emplace_back(SoundLoadWorker, filePath);
}

bool Init() {
    if (!Config::g_settings.OptAudioDecodeMt) {
        Log("[AsyncSoundLoader] DISABLED via configuration");
        return true;
    }
    g_active = true;
    Log("[AsyncSoundLoader] Active - Asynchronous Sound FX Loader Initialized");
    return true;
}

void Shutdown() {
    if (!Config::g_settings.OptAudioDecodeMt) return;
    g_active = false;
    for (auto& t : g_workerThreads) {
        if (t.joinable()) t.join();
    }
    g_workerThreads.clear();
    std::lock_guard<std::mutex> lock(g_soundMutex);
    g_soundCache.clear();
    Log("[AsyncSoundLoader] Stats: Preloaded %lld sound effects", g_preloads);
}

} // namespace AsyncSoundLoader
