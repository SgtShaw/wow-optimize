#include "async_tex_loader.h"
#include "MinHook.h"
#include "version.h"
#include <windows.h>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <unordered_set>

extern "C" void Log(const char* fmt, ...);

namespace AsyncTexLoader {

// Storm API exports
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

static std::vector<HANDLE> g_privateArchives;

// Thread variables
static std::thread g_workerThread;
static std::queue<std::string> g_prefetchQueue;
static std::mutex g_queueMutex;
static std::condition_variable g_queueCv;
static std::atomic<bool> g_shutdown{false};

static std::unordered_set<std::string> g_prefetchedTextures;
static SRWLOCK g_cacheLock = SRWLOCK_INIT;

// Hook definitions
typedef int (__cdecl *TexCreateBLP_fn)(char* path, int flags, int a3);
static TexCreateBLP_fn orig_TexCreateBLP = nullptr;

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
        if (pSFileOpenArchive(name, 0, 0, &hArchive)) {
            g_privateArchives.push_back(hArchive);
        }
    }
}

static void WorkerProc() {
    char readBuffer[65536];

    while (!g_shutdown.load(std::memory_order_relaxed)) {
        std::string filename;
        {
            std::unique_lock<std::mutex> lock(g_queueMutex);
            g_queueCv.wait_for(lock, std::chrono::milliseconds(50), [] {
                return !g_prefetchQueue.empty() || g_shutdown.load();
            });
            if (g_shutdown.load() && g_prefetchQueue.empty()) break;
            if (g_prefetchQueue.empty()) continue;
            filename = std::move(g_prefetchQueue.front());
            g_prefetchQueue.pop();
        }

        // Check cache under shared lock
        AcquireSRWLockShared(&g_cacheLock);
        bool alreadyDone = (g_prefetchedTextures.find(filename) != g_prefetchedTextures.end());
        ReleaseSRWLockShared(&g_cacheLock);
        if (alreadyDone) continue;

        // Try to pre-read file
        for (HANDLE hArchive : g_privateArchives) {
            HANDLE hFile = nullptr;
            if (pSFileOpenFileEx(hArchive, filename.c_str(), 0, &hFile)) {
                DWORD bytesRead = 0;
                while (pSFileReadFile(hFile, readBuffer, sizeof(readBuffer), &bytesRead, NULL) && bytesRead > 0) {
                    // Force cache fetch
                }
                pSFileCloseFile(hFile);

                AcquireSRWLockExclusive(&g_cacheLock);
                g_prefetchedTextures.insert(filename);
                ReleaseSRWLockExclusive(&g_cacheLock);
                break;
            }
        }
    }
}

static void PredictSurroundingTextures(const char* path) {
    // Detect Minimap tiles e.g., "World\Minimap\Azeroth\map32_48.blp"
    std::string strPath(path);
    size_t minimapPos = strPath.find("World\\Minimap\\");
    if (minimapPos != std::string::npos) {
        size_t mapPos = strPath.find("map");
        if (mapPos != std::string::npos) {
            int x = 0, y = 0;
            char mapName[64] = {0};
            // Extract mapName, x, and y
            // path format: "...[continent]\map[X]_[Y].blp"
            std::string sub = strPath.substr(mapPos + 3); // strip "map"
            size_t underscore = sub.find('_');
            size_t dot = sub.find('.');
            if (underscore != std::string::npos && dot != std::string::npos) {
                x = std::stoi(sub.substr(0, underscore));
                y = std::stoi(sub.substr(underscore + 1, dot - underscore - 1));

                std::string continent = strPath.substr(minimapPos + 14, mapPos - (minimapPos + 14) - 1);

                // Queue surrounding 8 tiles
                std::lock_guard<std::mutex> lock(g_queueMutex);
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        if (dx == 0 && dy == 0) continue;
                        char nextTile[256];
                        sprintf_s(nextTile, "World\\Minimap\\%s\\map%d_%d.blp", continent.c_str(), x + dx, y + dy);
                        g_prefetchQueue.push(nextTile);
                    }
                }
                g_queueCv.notify_one();
            }
        }
    }
}

static int __cdecl Hooked_TexCreateBLP(char* path, int flags, int a3) {
    if (path) {
        #if !TEST_DISABLE_TEXTURE_DECODE_MT
        PredictSurroundingTextures(path);
        #endif
    }
    int result = orig_TexCreateBLP(path, flags, a3);
    #if !TEST_DISABLE_TEXTURE_DECODE_MT
    if (result == 0 && path) {
        Log("[AsyncTexLoader] ERROR: TexCreateBLP failed to load texture '%s' (flags=%d, a3=%d)", path, flags, a3);
    }
    #endif
    return result;
}

static bool CheckPrologue(void* target, unsigned char* outPrologue) {
    __try {
        memcpy(outPrologue, target, 3);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Init() {
    if (!LoadStormAPIs()) return false;
    OpenPrivateArchives();

    g_shutdown = false;
    g_workerThread = std::thread(WorkerProc);

    void* target = (void*)0x004B8910;
    
    unsigned char prologue[3];
    if (!CheckPrologue(target, prologue)) {
        Log("[AsyncTexLoader] Target 0x004B8910 not readable.");
        return true;
    }

    if (prologue[0] != 0x55 || prologue[1] != 0x8B || prologue[2] != 0xEC) {
        Log("[AsyncTexLoader] BAD PROLOGUE at 0x004B8910. Skipping hook.");
        return true;
    }

    if (MH_CreateHook(target, (void*)Hooked_TexCreateBLP, (void**)&orig_TexCreateBLP) == MH_OK) {
        if (MH_EnableHook(target) == MH_OK) {
            Log("[AsyncTexLoader] Active - Minimap and model texture prefetcher running.");
            return true;
        }
        MH_RemoveHook(target);
    }

    return false;
}

void Shutdown() {
    g_shutdown.store(true);
    g_queueCv.notify_all();
    if (g_workerThread.joinable()) {
        g_workerThread.join();
    }

    for (HANDLE hArchive : g_privateArchives) {
        pSFileCloseArchive(hArchive);
    }
    g_privateArchives.clear();

    MH_DisableHook((void*)0x004B8910);
}

} // namespace AsyncTexLoader
