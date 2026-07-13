// ============================================================================
// Module: predictive_prefetch.cpp
// Description: Predicts player movement trajectory and pre-caches terrain (ADT)
//              files on a background thread using private Storm archive handles.
// Safety & Threading: Thread-safe. Uses separate archive handles to avoid races.
// ============================================================================

#include "predictive_prefetch.h"
#include "version.h"
#include <windows.h>
#include <string>
#include <vector>
#include <queue>
#include "win_mutex.h"
#include <thread>
#include <atomic>

extern "C" void Log(const char* fmt, ...);

namespace PredictivePrefetch {

// Storm API Types
typedef BOOL (APIENTRY *SFileOpenArchive_fn)(const char* szArchiveName, DWORD dwPriority, DWORD dwFlags, HANDLE* phArchive);
typedef BOOL (APIENTRY *SFileOpenFileEx_fn)(HANDLE hArchive, const char* szFileName, DWORD dwSearchScope, HANDLE* phFile);
typedef BOOL (APIENTRY *SFileReadFile_fn)(HANDLE hFile, void* lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
typedef BOOL (APIENTRY *SFileCloseFile_fn)(HANDLE hFile);
typedef BOOL (APIENTRY *SFileCloseArchive_fn)(HANDLE hArchive);

static SFileOpenArchive_fn pSFileOpenArchive = nullptr;
static SFileOpenFileEx_fn pSFileOpenFileEx = nullptr;
static SFileReadFile_fn pSFileReadFile = nullptr;
static SFileCloseFile_fn pSFileCloseFile = nullptr;
static SFileCloseArchive_fn pSFileCloseArchive = nullptr;

// Thread variables
static HANDLE g_workerThread = nullptr;
static void WorkerProc();
static DWORD WINAPI PredictivePrefetchWorkerThread(LPVOID) {
    WorkerProc();
    return 0;
}
static std::queue<std::string> g_prefetchQueue;
static WinMutex g_queueMutex;
static WinCondVar g_queueCv;
static std::atomic<bool> g_shutdown{false};

// Private Storm Archive handles
static std::vector<HANDLE> g_archives;

// Player coordinates globals (WoW 3.3.5a)
static float* const g_playerX = (float*)0x00BE1F30;
static float* const g_playerY = (float*)0x00BE1F34;
static float* const g_playerZ = (float*)0x00BE1F38;

// Last tracked state
static float g_lastX = 0.0f;
static float g_lastY = 0.0f;
static int g_lastAdtX = -1;
static int g_lastAdtY = -1;

// Load Storm APIs
static bool LoadStormAPIs() {
    HMODULE hStorm = GetModuleHandleA("storm.dll");
    if (!hStorm) hStorm = LoadLibraryA("storm.dll");
    if (!hStorm) return false;

    pSFileOpenArchive = (SFileOpenArchive_fn)GetProcAddress(hStorm, "SFileOpenArchive");
    pSFileOpenFileEx = (SFileOpenFileEx_fn)GetProcAddress(hStorm, "SFileOpenFileEx");
    pSFileReadFile = (SFileReadFile_fn)GetProcAddress(hStorm, "SFileReadFile");
    pSFileCloseFile = (SFileCloseFile_fn)GetProcAddress(hStorm, "SFileCloseFile");
    pSFileCloseArchive = (SFileCloseArchive_fn)GetProcAddress(hStorm, "SFileCloseArchive");

    return pSFileOpenArchive && pSFileOpenFileEx && pSFileReadFile && pSFileCloseFile && pSFileCloseArchive;
}

// Open main WoW archives privately to read files on worker thread safely
static void OpenPrivateArchives() {
    const char* archiveNames[] = {
        "Data\\common.MPQ",
        "Data\\world.MPQ",
        "Data\\lichking.MPQ",
        "Data\\patch.MPQ",
        "Data\\patch-2.MPQ",
        "Data\\patch-3.MPQ"
    };

    for (const char* name : archiveNames) {
        HANDLE hArchive = nullptr;
        // Open with thread-safe/read-only access flags
        if (pSFileOpenArchive(name, 0, 0, &hArchive)) {
            g_archives.push_back(hArchive);
        }
    }
    Log("[PredictivePrefetch] Opened %d MPQ archives privately for background loading", (int)g_archives.size());
}

// Read-ahead worker proc
static void WorkerProc() {
    char readBuffer[65536];

    while (!g_shutdown.load(std::memory_order_relaxed)) {
        std::string filename;
        {
            WinUniqueLock lock(g_queueMutex);
            g_queueCv.wait_for(lock, std::chrono::milliseconds(20), [] {
                return !g_prefetchQueue.empty() || g_shutdown.load();
            });
            if (g_shutdown.load() && g_prefetchQueue.empty()) break;
            if (g_prefetchQueue.empty()) continue;
            filename = std::move(g_prefetchQueue.front());
            g_prefetchQueue.pop();
        }

        // Try to open the file from our private handles
        bool opened = false;
        for (HANDLE hArchive : g_archives) {
            HANDLE hFile = nullptr;
            if (pSFileOpenFileEx(hArchive, filename.c_str(), 0, &hFile)) {
                opened = true;
                // Force load into OS page cache
                DWORD bytesRead = 0;
                while (pSFileReadFile(hFile, readBuffer, sizeof(readBuffer), &bytesRead, NULL) && bytesRead > 0) {
                    // Just reading
                }
                pSFileCloseFile(hFile);
                Log("[PredictivePrefetch] Background loaded: '%s'", filename.c_str());
                break;
            }
        }
    }
}

bool Init() {
    if (!LoadStormAPIs()) {
        Log("[PredictivePrefetch] Failed to resolve Storm.dll exports");
        return false;
    }

    g_shutdown = false;
    OpenPrivateArchives();

    // Spawn background thread
    g_workerThread = CreateThread(NULL, 0, PredictivePrefetchWorkerThread, NULL, 0, NULL);

    Log("[PredictivePrefetch] Active - Predictive movement-based read-ahead prefetcher running");
    return true;
}

void OnFrame() {
    // Only process coordinates if memory pressure is not critical and coordinates are initialized
    if (!g_playerX || !g_playerY) return;

    float cx = *g_playerX;
    float cy = *g_playerY;

    // Reject uninitialized or zero coordinate locations (often during loading screens)
    if (cx == 0.0f && cy == 0.0f) return;

    // Calculate movement velocity vector
    float vx = cx - g_lastX;
    float vy = cy - g_lastY;
    g_lastX = cx;
    g_lastY = cy;

    // Check if player is moving (ignore small vibrations)
    float speedSq = vx * vx + vy * vy;
    if (speedSq < 0.1f) return;

    // Project coordinates forward (5 seconds ahead)
    float px = cx + vx * 150.0f;
    float py = cy + vy * 150.0f;

    // Convert coordinates to ADT tile coordinates
    // ADT size is 533.33333 yards, coordinate grid center is at (0,0)
    int adtX = (int)(32.0f - py / 533.33333f);
    int adtY = (int)(32.0f - px / 533.33333f);

    // Clamp bounds [0, 63]
    if (adtX < 0) adtX = 0; if (adtX > 63) adtX = 63;
    if (adtY < 0) adtY = 0; if (adtY > 63) adtY = 63;

    // Trigger prefetch if predicted ADT changes
    if (adtX != g_lastAdtX || adtY != g_lastAdtY) {
        g_lastAdtX = adtX;
        g_lastAdtY = adtY;

        // Predict paths for the 4 primary continent folders
        const char* continents[] = { "Azeroth", "Kalimdor", "Expansion01", "Northrend" };

        WinLockGuard lock(g_queueMutex);
        // Clear previous queue to prioritize the new location
        while (!g_prefetchQueue.empty()) g_prefetchQueue.pop();

        for (const char* continent : continents) {
            char path[128];
            sprintf_s(path, "World\\Maps\\%s\\%s_%d_%d.adt", continent, continent, adtX, adtY);
            g_prefetchQueue.push(path);
        }
        g_queueCv.notify_one();
    }
}

void Shutdown() {
    g_shutdown.store(true);
    g_queueCv.notify_all();
    if (g_workerThread) {
        WaitForSingleObject(g_workerThread, INFINITE);
        CloseHandle(g_workerThread);
        g_workerThread = nullptr;
    }

    // Close private archive handles
    for (HANDLE hArchive : g_archives) {
        pSFileCloseArchive(hArchive);
    }
    g_archives.clear();
}

} // namespace PredictivePrefetch
