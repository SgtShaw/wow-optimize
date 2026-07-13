#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <unordered_map>
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace SavedVarsPreloadAsync {

struct CachedFile {
    std::string content;
    bool loaded;
};

static std::unordered_map<std::string, CachedFile> g_fileCache;
static std::mutex g_cacheMutex;
static std::vector<HANDLE> g_preloadThreads;
static void PreloadWorker(std::string filePath);
static DWORD WINAPI PreloadWorkerThread(LPVOID lpParam) {
    std::string* pPath = static_cast<std::string*>(lpParam);
    if (pPath) {
        PreloadWorker(*pPath);
        delete pPath;
    }
    return 0;
}
static bool g_active = false;

// Worker function to load a file in the background
void PreloadWorker(std::string filePath) {
    HANDLE hFile = CreateFileA(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    DWORD size = GetFileSize(hFile, NULL);
    if (size > 0 && size < 16 * 1024 * 1024) { // Only load files under 16MB
        std::vector<char> buffer(size);
        DWORD readBytes = 0;
        if (ReadFile(hFile, buffer.data(), size, &readBytes, NULL)) {
            std::lock_guard<std::mutex> lock(g_cacheMutex);
            g_fileCache[filePath] = { std::string(buffer.data(), readBytes), true };
        }
    }
    CloseHandle(hFile);
}

void QueuePreload(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    if (g_fileCache.find(filePath) != g_fileCache.end()) return; // Already queued or loaded
    g_fileCache[filePath] = { "", false };
    
    std::string* pPath = new std::string(filePath);
    HANDLE h = CreateThread(NULL, 0, PreloadWorkerThread, pPath, 0, NULL);
    if (h) {
        g_preloadThreads.push_back(h);
    } else {
        delete pPath;
    }
}

bool GetPreloadedContent(const std::string& filePath, std::string* outContent) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    auto it = g_fileCache.find(filePath);
    if (it != g_fileCache.end() && it->second.loaded) {
        *outContent = it->second.content;
        return true;
    }
    return false;
}

bool Init() {
    g_active = true;
    Log("[SavedVarsPreloadAsync] Active - Asynchronous SavedVariables preloader initialized");
    return true;
}

void Shutdown() {
    g_active = false;
    for (HANDLE h : g_preloadThreads) {
        if (h) {
            WaitForSingleObject(h, INFINITE);
            CloseHandle(h);
        }
    }
    g_preloadThreads.clear();
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_fileCache.clear();
}

} // namespace SavedVarsPreloadAsync
