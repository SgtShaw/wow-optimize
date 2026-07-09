#include "async_tex_loader.h"
#include "core/config.h"
#include "../allocators/loading_defrag.h"
#include "../../build/_deps/minhook-src/include/MinHook.h"
#include <windows.h>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <unordered_set>

extern "C" void Log(const char* fmt, ...);

namespace AsyncTexLoader {

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

static SFileOpenFileEx_fn orig_SFileOpenFileEx = nullptr;
static SFileReadFile_fn orig_SFileReadFile = nullptr;
static SFileCloseFile_fn orig_SFileCloseFile = nullptr;

static std::vector<HANDLE> g_privateArchives;

// Thread variables
static std::thread g_workerThread;
static std::queue<std::string> g_prefetchQueue;
static std::mutex g_queueMutex;
static std::condition_variable g_queueCv;
static std::atomic<bool> g_shutdown{false};
static DWORD g_workerThreadId = 0;

// Virtual File System (VFS) structures
struct VirtualFile {
    std::string path;
    std::vector<char> data;
    size_t offset = 0;
};

static std::unordered_map<HANDLE, VirtualFile*> g_virtualHandles;
static std::mutex g_virtualHandlesMutex;
static HANDLE g_nextVirtualHandle = (HANDLE)0xFEED0000;

static std::unordered_map<std::string, std::vector<char>> g_fileMemoryCache;
static std::mutex g_fileCacheMutex;

// Hot-swapping structures
struct PlaceholderEntry {
    int placeholderHandle;
    std::string realPath;
    unsigned int flags;
    void* a3;
    int a4;
};

static std::unordered_map<std::string, PlaceholderEntry> g_placeholderMap;
static std::unordered_set<std::string> g_hotSwapTextures;
static std::mutex g_swapMutex;

static std::vector<char> g_whiteTextureBytes;

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

static bool LoadPlaceholderTextureBytes() {
    for (HANDLE hArchive : g_privateArchives) {
        HANDLE hFile = nullptr;
        if (pSFileOpenFileEx(hArchive, "Interface\\Buttons\\WHITE8X8.blp", 0, &hFile)) {
            DWORD size = GetFileSize(hFile, NULL);
            if (size != INVALID_FILE_SIZE && size > 0) {
                g_whiteTextureBytes.resize(size);
                DWORD read = 0;
                if (pSFileReadFile(hFile, g_whiteTextureBytes.data(), size, &read, NULL) && read == size) {
                    pSFileCloseFile(hFile);
                    return true;
                }
            }
            pSFileCloseFile(hFile);
        }
    }
    return false;
}

// Storm API Hook detours
BOOL APIENTRY Hooked_SFileOpenFileEx(HANDLE hArchive, const char* szFileName, DWORD dwSearchScope, HANDLE* phFile) {
    if (GetCurrentThreadId() == g_workerThreadId) {
        return orig_SFileOpenFileEx(hArchive, szFileName, dwSearchScope, phFile);
    }

    if (szFileName) {
        std::string path(szFileName);

        std::lock_guard<std::mutex> lock(g_fileCacheMutex);
        if (g_fileMemoryCache.count(path)) {
            VirtualFile* vf = new VirtualFile();
            vf->path = path;
            vf->data = g_fileMemoryCache[path];
            vf->offset = 0;

            std::lock_guard<std::mutex> hLock(g_virtualHandlesMutex);
            HANDLE hVirtual = g_nextVirtualHandle;
            g_nextVirtualHandle = (HANDLE)((uintptr_t)g_nextVirtualHandle + 1);
            g_virtualHandles[hVirtual] = vf;

            if (phFile) {
                *phFile = hVirtual;
            }
            return TRUE;
        }
    }

    return orig_SFileOpenFileEx(hArchive, szFileName, dwSearchScope, phFile);
}

BOOL APIENTRY Hooked_SFileReadFile(HANDLE hFile, void* lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped) {
    if (GetCurrentThreadId() == g_workerThreadId) {
        return orig_SFileReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
    }

    {
        std::lock_guard<std::mutex> hLock(g_virtualHandlesMutex);
        if (g_virtualHandles.count(hFile)) {
            VirtualFile* vf = g_virtualHandles[hFile];
            if (!vf) return FALSE;

            size_t available = vf->data.size() - vf->offset;
            size_t toCopy = (nNumberOfBytesToRead < available) ? nNumberOfBytesToRead : available;

            if (toCopy > 0) {
                memcpy(lpBuffer, vf->data.data() + vf->offset, toCopy);
                vf->offset += toCopy;
            }

            if (lpNumberOfBytesRead) {
                *lpNumberOfBytesRead = (DWORD)toCopy;
            }
            return TRUE;
        }
    }

    return orig_SFileReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
}

BOOL APIENTRY Hooked_SFileCloseFile(HANDLE hFile) {
    if (GetCurrentThreadId() == g_workerThreadId) {
        return orig_SFileCloseFile(hFile);
    }

    {
        std::lock_guard<std::mutex> hLock(g_virtualHandlesMutex);
        if (g_virtualHandles.count(hFile)) {
            VirtualFile* vf = g_virtualHandles[hFile];
            delete vf;
            g_virtualHandles.erase(hFile);
            return TRUE;
        }
    }

    return orig_SFileCloseFile(hFile);
}

static void WorkerProc() {
    g_workerThreadId = GetCurrentThreadId();
    std::vector<char> readBuffer(262144);

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

        bool alreadyLoaded = false;
        {
            std::lock_guard<std::mutex> cacheLock(g_fileCacheMutex);
            if (g_fileMemoryCache.count(filename) && g_fileMemoryCache[filename].size() != g_whiteTextureBytes.size()) {
                alreadyLoaded = true;
            }
        }
        if (alreadyLoaded) continue;

        // Load the file data from MPQ into memory
        std::vector<char> fileData;
        bool loaded = false;

        for (HANDLE hArchive : g_privateArchives) {
            HANDLE hFile = nullptr;
            if (pSFileOpenFileEx(hArchive, filename.c_str(), 0, &hFile)) {
                DWORD size = GetFileSize(hFile, NULL);
                if (size != INVALID_FILE_SIZE && size > 0) {
                    fileData.resize(size);
                    DWORD read = 0;
                    if (pSFileReadFile(hFile, fileData.data(), size, &read, NULL) && read == size) {
                        loaded = true;
                    }
                }
                pSFileCloseFile(hFile);
            }
            if (loaded) break;
        }

        if (loaded) {
            std::lock_guard<std::mutex> cacheLock(g_fileCacheMutex);
            g_fileMemoryCache[filename] = std::move(fileData);
        }
    }
}

static bool IsCacheableAsset(const char* path) {
    if (!path) return false;
    std::string p(path);
    if (p.find("Interface\\") == 0 || p.find("interface\\") == 0) {
        return false;
    }
    if (p.find(".blp") != std::string::npos || p.find(".BLP") != std::string::npos) {
        return true;
    }
    return false;
}

int Call_Orig_TexCreateBLP(unsigned int flags, char* path, void* a3, int a4) {
    int result;
    __asm {
        push a4
        push a3
        push path
        mov eax, flags
        call orig_TexCreateBLP
        mov result, eax
    }
    return result;
}

// C++ Handler for BLP creation
int __stdcall Handle_TexCreateBLP(unsigned int flags, char* path, void* a3, int a4) {
    if (!path) return 0;

    if (LoadingDefrag::IsLoadingActive() || !IsCacheableAsset(path)) {
        return Call_Orig_TexCreateBLP(flags, path, a3, a4);
    }

    std::string realPath(path);

    // Check if successfully swapped already
    {
        std::lock_guard<std::mutex> lock(g_swapMutex);
        if (g_hotSwapTextures.count(realPath)) {
            return Call_Orig_TexCreateBLP(flags, path, a3, a4);
        }
    }

    // Check if already in progress
    {
        std::lock_guard<std::mutex> lock(g_swapMutex);
        if (g_placeholderMap.count(realPath)) {
            return g_placeholderMap[realPath].placeholderHandle;
        }
    }

    // Allocate placeholder white texture bytes initially under the target path
    {
        std::lock_guard<std::mutex> cacheLock(g_fileCacheMutex);
        g_fileMemoryCache[realPath] = g_whiteTextureBytes;
    }

    int placeholder = Call_Orig_TexCreateBLP(flags, path, a3, a4);
    if (!placeholder) {
        return 0;
    }

    // Set real path in HTEXTURE name buffer (offset 108)
    char* nameBuf = (char*)(placeholder + 108);
    strncpy_s(nameBuf, 256, path, _TRUNCATE);

    // Queue real file read in background
    {
        std::lock_guard<std::mutex> lock(g_swapMutex);
        PlaceholderEntry entry;
        entry.placeholderHandle = placeholder;
        entry.realPath = realPath;
        entry.flags = flags;
        entry.a3 = a3;
        entry.a4 = a4;
        g_placeholderMap[realPath] = entry;

        {
            std::lock_guard<std::mutex> qLock(g_queueMutex);
            g_prefetchQueue.push(realPath);
            g_queueCv.notify_one();
        }
    }

    return placeholder;
}

// Naked detour wrapper to preserve registers and calling conventions
__declspec(naked) void Hooked_TexCreateBLP_Naked() {
    __asm {
        push [esp + 12] // push a4
        push [esp + 12] // push a3
        push [esp + 12] // push path
        push eax        // push flags (EAX)
        call Handle_TexCreateBLP
        ret 12
    }
}

void OnFrame() {
    if (!Config::g_settings.OptAsyncTexLoader) return;
#if TEST_DISABLE_TEXTURE_DECODE_MT
    return;
#endif
    std::vector<PlaceholderEntry> completed;

    {
        std::lock_guard<std::mutex> lock(g_swapMutex);
        std::lock_guard<std::mutex> cacheLock(g_fileCacheMutex);

        auto it = g_placeholderMap.begin();
        while (it != g_placeholderMap.end()) {
            if (g_fileMemoryCache.count(it->first) && 
                g_fileMemoryCache[it->first].size() != g_whiteTextureBytes.size()) {
                completed.push_back(it->second);
                it = g_placeholderMap.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (const auto& entry : completed) {
        std::string tempPath = entry.realPath + "_tmp";

        {
            std::lock_guard<std::mutex> cacheLock(g_fileCacheMutex);
            g_fileMemoryCache[tempPath] = g_fileMemoryCache[entry.realPath];
        }

        int real = Call_Orig_TexCreateBLP(entry.flags, const_cast<char*>(tempPath.c_str()), entry.a3, entry.a4);
        if (real) {
            // Swap wrapper pointers
            int placeholderD3D = *(int*)(entry.placeholderHandle + 68);
            int realD3D = *(int*)(real + 68);

            *(int*)(entry.placeholderHandle + 68) = realD3D;

            *(short*)(entry.placeholderHandle + 76) = *(short*)(real + 76);
            *(short*)(entry.placeholderHandle + 78) = *(short*)(real + 78);
            *(int*)(entry.placeholderHandle + 80) = *(int*)(real + 80);
            *(int*)(entry.placeholderHandle + 84) = *(int*)(real + 84);

            // Reclaim placeholder D3D resource via temp object release
            *(int*)(real + 68) = placeholderD3D;

            {
                std::lock_guard<std::mutex> lock(g_swapMutex);
                g_hotSwapTextures.insert(entry.realPath);
            }
        }

        {
            std::lock_guard<std::mutex> cacheLock(g_fileCacheMutex);
            g_fileMemoryCache.erase(tempPath);
        }
    }
}

bool Init() {
    if (!Config::g_settings.OptAsyncTexLoader) {
        Log("[AsyncTexLoader] DISABLED via configuration");
        return true;
    }
#if TEST_DISABLE_TEXTURE_DECODE_MT
    Log("[AsyncTexLoader] Disabled via TEST_DISABLE_TEXTURE_DECODE_MT");
    return true;
#endif
    if (!LoadStormAPIs()) return false;
    OpenPrivateArchives();

    if (!LoadPlaceholderTextureBytes()) {
        Log("[AsyncTexLoader] Failed to pre-load WHITE8X8 placeholder bytes.");
        return false;
    }

    g_shutdown = false;
    g_workerThread = std::thread(WorkerProc);

    HMODULE hStorm = GetModuleHandleA("storm.dll");
    void* pOpenFile = (void*)GetProcAddress(hStorm, "SFileOpenFileEx");
    void* pReadFile = (void*)GetProcAddress(hStorm, "SFileReadFile");
    void* pCloseFile = (void*)GetProcAddress(hStorm, "SFileCloseFile");

    if (MH_CreateHook(pOpenFile, (void*)Hooked_SFileOpenFileEx, (void**)&orig_SFileOpenFileEx) != MH_OK ||
        MH_CreateHook(pReadFile, (void*)Hooked_SFileReadFile, (void**)&orig_SFileReadFile) != MH_OK ||
        MH_CreateHook(pCloseFile, (void*)Hooked_SFileCloseFile, (void**)&orig_SFileCloseFile) != MH_OK) {
        Log("[AsyncTexLoader] Failed to create Storm hooks.");
        return false;
    }

    void* target = (void*)0x004B8910;
    if (MH_CreateHook(target, (void*)Hooked_TexCreateBLP_Naked, (void**)&orig_TexCreateBLP) != MH_OK) {
        Log("[AsyncTexLoader] Failed to create TexCreateBLP hook.");
        return false;
    }

    if (MH_EnableHook(pOpenFile) != MH_OK ||
        MH_EnableHook(pReadFile) != MH_OK ||
        MH_EnableHook(pCloseFile) != MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        Log("[AsyncTexLoader] Failed to enable hooks.");
        return false;
    }

    Log("[AsyncTexLoader] Active - VFS redirector and hot-swapper active.");
    return true;
}

void Shutdown() {
    if (!Config::g_settings.OptAsyncTexLoader) return;
#if TEST_DISABLE_TEXTURE_DECODE_MT
    return;
#endif
    g_shutdown.store(true);
    g_queueCv.notify_all();
    if (g_workerThread.joinable()) {
        g_workerThread.join();
    }

    HMODULE hStorm = GetModuleHandleA("storm.dll");
    void* pOpenFile = (void*)GetProcAddress(hStorm, "SFileOpenFileEx");
    void* pReadFile = (void*)GetProcAddress(hStorm, "SFileReadFile");
    void* pCloseFile = (void*)GetProcAddress(hStorm, "SFileCloseFile");
    void* target = (void*)0x004B8910;

    MH_DisableHook(pOpenFile);
    MH_DisableHook(pReadFile);
    MH_DisableHook(pCloseFile);
    MH_DisableHook(target);

    for (HANDLE hArchive : g_privateArchives) {
        pSFileCloseArchive(hArchive);
    }
    g_privateArchives.clear();

    std::lock_guard<std::mutex> cacheLock(g_fileCacheMutex);
    g_fileMemoryCache.clear();

    std::lock_guard<std::mutex> lock(g_virtualHandlesMutex);
    for (auto pair : g_virtualHandles) {
        delete pair.second;
    }
    g_virtualHandles.clear();

    std::lock_guard<std::mutex> swapLock(g_swapMutex);
    g_placeholderMap.clear();
    g_hotSwapTextures.clear();
}

} // namespace AsyncTexLoader
