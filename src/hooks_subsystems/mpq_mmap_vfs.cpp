// ============================================================================
// Module: mpq_mmap_vfs.cpp
// Description: Parallelized MPQ asset preloader and in-memory VFS cache.
//              Maps file reading to background threads to avoid main thread stutters.
// ============================================================================

#include "mpq_mmap_vfs.h"
#include "MinHook.h"
#include "version.h"
#include "config.h"
#include "async_tex_loader.h"
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>

extern "C" void Log(const char* fmt, ...);

namespace MpqMmapVfs {

// ---- Storm API Types ----
typedef BOOL (APIENTRY *SFileOpenArchive_fn)(const char* szArchiveName, DWORD dwPriority, DWORD dwFlags, HANDLE* phArchive);
typedef BOOL (APIENTRY *SFileOpenFileEx_fn)(HANDLE hArchive, const char* szFileName, DWORD dwSearchScope, HANDLE* phFile);
typedef BOOL (APIENTRY *SFileReadFile_fn)(HANDLE hFile, void* lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
typedef BOOL (APIENTRY *SFileCloseFile_fn)(HANDLE hFile);
typedef BOOL (APIENTRY *SFileCloseArchive_fn)(HANDLE hArchive);
typedef BOOL (APIENTRY *SFileGetFileSize_fn)(HANDLE hFile, LPDWORD pdwFileSizeHigh);

static SFileOpenArchive_fn pSFileOpenArchive = nullptr;
static SFileOpenFileEx_fn pSFileOpenFileEx = nullptr;
static SFileReadFile_fn pSFileReadFile = nullptr;
static SFileCloseFile_fn pSFileCloseFile = nullptr;
static SFileCloseArchive_fn pSFileCloseArchive = nullptr;
static SFileGetFileSize_fn pSFileGetFileSize = nullptr;

// Hook Trampolines
static SFileOpenFileEx_fn orig_SFileOpenFileEx = nullptr;
static SFileReadFile_fn orig_SFileReadFile = nullptr;
static SFileCloseFile_fn orig_SFileCloseFile = nullptr;

// ---- Memory Map Handles for MPQ OS Caching ----
struct MpqMapping {
    HANDLE hFile;
    HANDLE hMapping;
    void* pView;
    size_t size;
};
static std::vector<MpqMapping> g_mpqMappings;

// ---- VFS Memory Cache ----
static std::unordered_map<std::string, std::vector<uint8_t>> g_vfsCache;
static std::mutex g_vfsCacheMutex;

// ---- Virtual Handle Manager ----
struct VirtualFile {
    std::string path;
    std::vector<uint8_t> data;
    size_t offset;
};
static std::unordered_map<HANDLE, VirtualFile*> g_virtualHandles;
static std::mutex g_handlesMutex;
static HANDLE g_nextVirtualHandle = (HANDLE)0xFEED0000;

// ---- Worker Thread Pool ----
static std::vector<HANDLE> g_workers;
static void DecompressorProc(int threadIdx);
static DWORD WINAPI DecompressorThreadProc(LPVOID lpParam) {
    int threadIdx = (int)(uintptr_t)lpParam;
    DecompressorProc(threadIdx);
    return 0;
}
static std::queue<std::string> g_preloadQueue;
static std::mutex g_queueMutex;
static std::condition_variable g_queueCv;
static std::atomic<bool> g_shutdown{false};
static std::atomic<DWORD> g_workerThreadId1{0};
static std::atomic<DWORD> g_workerThreadId2{0};

// ---- Private Archive Handles for worker thread reads ----
static std::vector<HANDLE> g_privateArchives;

// ---- Stats Counters ----
static std::atomic<long> g_filesCached{0};
static std::atomic<long> g_cacheHits{0};
static std::atomic<long> g_cacheMisses{0};
static std::atomic<double> g_totalLoadTimeMs{0.0};
static double g_qpcFreqMs = 0.0;
static bool g_initialized = false;

// Helper to normalize file paths
static std::string NormalizePath(const std::string& path) {
    std::string result = path;
    for (char& c : result) {
        if (c == '/') c = '\\';
        else c = tolower(c);
    }
    return result;
}

// Memory-map the MPQ archive files to keep their pages hot in the OS cache
static void MapArchivePages(const char* filepath) {
    MpqMapping mapping = { INVALID_HANDLE_VALUE, NULL, nullptr, 0 };
    mapping.hFile = CreateFileA(filepath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (mapping.hFile == INVALID_HANDLE_VALUE) return;

    LARGE_INTEGER fs;
    if (GetFileSizeEx(mapping.hFile, &fs)) {
        mapping.size = (size_t)fs.QuadPart;
        // Map only the first 256MB of massive MPQs to prevent 32-bit address space exhaustion
        size_t mapSize = (mapping.size > 268435456) ? 268435456 : mapping.size;

        mapping.hMapping = CreateFileMappingA(mapping.hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (mapping.hMapping) {
            mapping.pView = MapViewOfFile(mapping.hMapping, FILE_MAP_READ, 0, 0, mapSize);
            if (mapping.pView) {
                g_mpqMappings.push_back(mapping);
                Log("[MpqMmapVfs] Cached & mapped %s (%llu MB in memory)", filepath, (unsigned long long)(mapSize / 1024 / 1024));
                return;
            }
            CloseHandle(mapping.hMapping);
        }
    }
    CloseHandle(mapping.hFile);
}

// Load Storm functions dynamically
static bool ResolveStormAPIs() {
    HMODULE hStorm = GetModuleHandleA("storm.dll");
    if (!hStorm) hStorm = LoadLibraryA("storm.dll");
    if (!hStorm) return false;

    pSFileOpenArchive   = (SFileOpenArchive_fn)GetProcAddress(hStorm, "SFileOpenArchive");
    pSFileOpenFileEx    = (SFileOpenFileEx_fn)GetProcAddress(hStorm, "SFileOpenFileEx");
    pSFileReadFile      = (SFileReadFile_fn)GetProcAddress(hStorm, "SFileReadFile");
    pSFileCloseFile     = (SFileCloseFile_fn)GetProcAddress(hStorm, "SFileCloseFile");
    pSFileCloseArchive  = (SFileCloseArchive_fn)GetProcAddress(hStorm, "SFileCloseArchive");
    pSFileGetFileSize   = (SFileGetFileSize_fn)GetProcAddress(hStorm, "SFileGetFileSize");

    return pSFileOpenArchive && pSFileOpenFileEx && pSFileReadFile && pSFileCloseFile && pSFileCloseArchive && pSFileGetFileSize;
}

// Hook Detours
BOOL APIENTRY Hooked_SFileOpenFileEx(HANDLE hArchive, const char* szFileName, DWORD dwSearchScope, HANDLE* phFile) {
    DWORD tid = GetCurrentThreadId();
    if (tid == g_workerThreadId1.load() || tid == g_workerThreadId2.load()) {
        return orig_SFileOpenFileEx(hArchive, szFileName, dwSearchScope, phFile);
    }

    if (szFileName) {
        std::string normPath = NormalizePath(szFileName);

        // 1. Check Async Texture Loader Cache first if enabled
        std::vector<uint8_t> texData;
        if (Config::g_settings.OptAsyncTexLoader && AsyncTexLoader::GetCachedTextureData(normPath, texData)) {
            VirtualFile* vf = new VirtualFile();
            vf->path = normPath;
            vf->data = std::move(texData);
            vf->offset = 0;

            std::lock_guard<std::mutex> hLock(g_handlesMutex);
            HANDLE hVirtual = g_nextVirtualHandle;
            g_nextVirtualHandle = (HANDLE)((uintptr_t)g_nextVirtualHandle + 1);
            g_virtualHandles[hVirtual] = vf;

            if (phFile) {
                *phFile = hVirtual;
            }
            g_cacheHits++;
            return TRUE;
        }

        // 2. Check general VFS Cache
        std::lock_guard<std::mutex> lock(g_vfsCacheMutex);
        auto it = g_vfsCache.find(normPath);
        if (it != g_vfsCache.end()) {
            VirtualFile* vf = new VirtualFile();
            vf->path = normPath;
            vf->data = it->second;
            vf->offset = 0;

            std::lock_guard<std::mutex> hLock(g_handlesMutex);
            HANDLE hVirtual = g_nextVirtualHandle;
            g_nextVirtualHandle = (HANDLE)((uintptr_t)g_nextVirtualHandle + 1);
            g_virtualHandles[hVirtual] = vf;

            if (phFile) {
                *phFile = hVirtual;
            }
            g_cacheHits++;
            return TRUE;
        }
    }

    g_cacheMisses++;
    return orig_SFileOpenFileEx(hArchive, szFileName, dwSearchScope, phFile);
}

BOOL APIENTRY Hooked_SFileReadFile(HANDLE hFile, void* lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped) {
    DWORD tid = GetCurrentThreadId();
    if (tid == g_workerThreadId1.load() || tid == g_workerThreadId2.load()) {
        return orig_SFileReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
    }

    VirtualFile* vf = nullptr;
    {
        std::lock_guard<std::mutex> hLock(g_handlesMutex);
        auto it = g_virtualHandles.find(hFile);
        if (it != g_virtualHandles.end()) {
            vf = it->second;
        }
    }

    if (vf) {
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

    return orig_SFileReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
}

BOOL APIENTRY Hooked_SFileCloseFile(HANDLE hFile) {
    DWORD tid = GetCurrentThreadId();
    if (tid == g_workerThreadId1.load() || tid == g_workerThreadId2.load()) {
        return orig_SFileCloseFile(hFile);
    }

    {
        std::lock_guard<std::mutex> hLock(g_handlesMutex);
        auto it = g_virtualHandles.find(hFile);
        if (it != g_virtualHandles.end()) {
            VirtualFile* vf = it->second;
            delete vf;
            g_virtualHandles.erase(it);
            return TRUE;
        }
    }

    return orig_SFileCloseFile(hFile);
}

// Background thread decompressor and cache builder
static void DecompressorProc(int threadIdx) {
    if (threadIdx == 1) g_workerThreadId1.store(GetCurrentThreadId());
    else g_workerThreadId2.store(GetCurrentThreadId());

    std::vector<uint8_t> readBuf(131072); // 128KB chunks

    while (!g_shutdown.load()) {
        std::string filename;
        {
            std::unique_lock<std::mutex> lock(g_queueMutex);
            g_queueCv.wait_for(lock, std::chrono::milliseconds(50), [&] {
                return !g_preloadQueue.empty() || g_shutdown.load();
            });

            if (g_shutdown.load() && g_preloadQueue.empty()) break;
            if (g_preloadQueue.empty()) continue;

            filename = std::move(g_preloadQueue.front());
            g_preloadQueue.pop();
        }

        std::string normPath = NormalizePath(filename);

        // Check if already in cache
        {
            std::lock_guard<std::mutex> lock(g_vfsCacheMutex);
            if (g_vfsCache.count(normPath)) continue;
        }

        LARGE_INTEGER start, end;
        QueryPerformanceCounter(&start);

        // Load the file using private handles to execute Storm decompression in background
        bool loaded = false;
        for (HANDLE hArchive : g_privateArchives) {
            HANDLE hFile = nullptr;
            if (pSFileOpenFileEx(hArchive, filename.c_str(), 0, &hFile)) {
                DWORD sizeLow = pSFileGetFileSize(hFile, NULL);
                if (sizeLow > 0 && sizeLow != INVALID_FILE_SIZE) {
                    std::vector<uint8_t> decompressedData;
                    decompressedData.reserve(sizeLow);

                    DWORD read = 0;
                    while (pSFileReadFile(hFile, readBuf.data(), (DWORD)readBuf.size(), &read, NULL) && read > 0) {
                        decompressedData.insert(decompressedData.end(), readBuf.data(), readBuf.data() + read);
                    }

                    pSFileCloseFile(hFile);

                    // Add to VFS cache
                    {
                        std::lock_guard<std::mutex> lock(g_vfsCacheMutex);
                        g_vfsCache[normPath] = std::move(decompressedData);
                    }
                    g_filesCached++;
                    loaded = true;
                    break;
                }
                pSFileCloseFile(hFile);
            }
        }

        if (loaded) {
            QueryPerformanceCounter(&end);
            double loadTime = (double)(end.QuadPart - start.QuadPart) / g_qpcFreqMs;
            g_totalLoadTimeMs.store(g_totalLoadTimeMs.load() + loadTime);
        }
    }
}

bool Init() {
    if (g_initialized) return true;

    if (!ResolveStormAPIs()) {
        Log("[MpqMmapVfs] Error: Failed to resolve Storm APIs");
        return false;
    }

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpcFreqMs = (double)freq.QuadPart / 1000.0;

    // 1. Establish Memory Maps for key game archives
    const char* archives[] = {
        "Data\\common.MPQ",
        "Data\\world.MPQ",
        "Data\\lichking.MPQ",
        "Data\\patch.MPQ",
        "Data\\patch-2.MPQ",
        "Data\\patch-3.MPQ"
    };
    for (const char* name : archives) {
        MapArchivePages(name);
    }

    // 2. Open private handles for background decompressor threads
    for (const char* name : archives) {
        HANDLE hArch = nullptr;
        if (pSFileOpenArchive(name, 0, 0, &hArch)) {
            g_privateArchives.push_back(hArch);
        }
    }

    // 3. Start decompressor worker pool
    g_shutdown = false;
    for (int i = 1; i <= 2; i++) {
        HANDLE h = CreateThread(NULL, 0, DecompressorThreadProc, (LPVOID)(uintptr_t)i, 0, NULL);
        if (h) g_workers.push_back(h);
    }

    // 4. Install SFile hooks
    void* pOpenFile = (void*)GetProcAddress(GetModuleHandleA("storm.dll"), "SFileOpenFileEx");
    void* pReadFile = (void*)GetProcAddress(GetModuleHandleA("storm.dll"), "SFileReadFile");
    void* pCloseFile = (void*)GetProcAddress(GetModuleHandleA("storm.dll"), "SFileCloseFile");

    if (MH_CreateHook(pOpenFile, (void*)Hooked_SFileOpenFileEx, (void**)&orig_SFileOpenFileEx) == MH_OK &&
        MH_CreateHook(pReadFile, (void*)Hooked_SFileReadFile, (void**)&orig_SFileReadFile) == MH_OK &&
        MH_CreateHook(pCloseFile, (void*)Hooked_SFileCloseFile, (void**)&orig_SFileCloseFile) == MH_OK) {
        
        if (MH_EnableHook(pOpenFile) == MH_OK &&
            MH_EnableHook(pReadFile) == MH_OK &&
            MH_EnableHook(pCloseFile) == MH_OK) {
            Log("[MpqMmapVfs] Hooks installed and active for Storm SFile interface");
        } else {
            Log("[MpqMmapVfs] Error: Failed to enable hooks");
            return false;
        }
    } else {
        Log("[MpqMmapVfs] Error: Failed to create hooks");
        return false;
    }

    g_initialized = true;
    Log("[MpqMmapVfs] [ OK ] Memory-Mapped MPQ VFS & Parallel Decompressor active");

    // 5. Preload critical DBC database files for instant RAM cache
    if (Config::g_settings.OptDbcPreload) {
        const char* dbcs[] = {
            "DBFilesClient\\Spell.dbc",
            "DBFilesClient\\Item.dbc",
            "DBFilesClient\\ItemDisplayInfo.dbc",
            "DBFilesClient\\Map.dbc",
            "DBFilesClient\\AreaTable.dbc",
            "DBFilesClient\\SoundEntries.dbc",
            "DBFilesClient\\Light.dbc",
            "DBFilesClient\\DungeonMap.dbc",
            "DBFilesClient\\DungeonMapChunk.dbc",
            "DBFilesClient\\Faction.dbc",
            "DBFilesClient\\FactionTemplate.dbc",
            "DBFilesClient\\ChrClasses.dbc",
            "DBFilesClient\\ChrRaces.dbc",
            "DBFilesClient\\CreatureModelData.dbc",
            "DBFilesClient\\CreatureDisplayInfo.dbc",
            "DBFilesClient\\EmotesText.dbc",
            "DBFilesClient\\Achievement.dbc",
            "DBFilesClient\\Achievement_Criteria.dbc",
            "DBFilesClient\\SpellCastTimes.dbc",
            "DBFilesClient\\SpellDuration.dbc",
            "DBFilesClient\\SpellRange.dbc",
            "DBFilesClient\\SpellRadius.dbc",
            "DBFilesClient\\SpellIcon.dbc",
            "DBFilesClient\\SpellCooldowns.dbc",
            "DBFilesClient\\AnimationData.dbc",
            "DBFilesClient\\Talent.dbc",
            "DBFilesClient\\TalentTab.dbc",
            "DBFilesClient\\GlueScreenTemplates.dbc",
            "DBFilesClient\\WorldMapArea.dbc",
            "DBFilesClient\\WorldMapOverlay.dbc"
        };
        for (const char* dbc : dbcs) {
            QueueFilePreload(dbc);
        }
        Log("[MpqMmapVfs] Queued %d critical DBC database files for background RAM caching", (int)(sizeof(dbcs) / sizeof(dbcs[0])));
    }

    return true;
}

void Shutdown() {
    if (!g_initialized) return;

    // Disable hooks
    void* pOpenFile = (void*)GetProcAddress(GetModuleHandleA("storm.dll"), "SFileOpenFileEx");
    void* pReadFile = (void*)GetProcAddress(GetModuleHandleA("storm.dll"), "SFileReadFile");
    void* pCloseFile = (void*)GetProcAddress(GetModuleHandleA("storm.dll"), "SFileCloseFile");

    MH_DisableHook(pOpenFile);
    MH_DisableHook(pReadFile);
    MH_DisableHook(pCloseFile);

    MH_RemoveHook(pOpenFile);
    MH_RemoveHook(pReadFile);
    MH_RemoveHook(pCloseFile);

    // Stop worker threads
    g_shutdown = true;
    g_queueCv.notify_all();
    for (HANDLE h : g_workers) {
        if (h) {
            WaitForSingleObject(h, INFINITE);
            CloseHandle(h);
        }
    }
    g_workers.clear();

    // Close private handles
    for (HANDLE hArch : g_privateArchives) {
        pSFileCloseArchive(hArch);
    }
    g_privateArchives.clear();

    // Clean memory maps
    for (auto& mapping : g_mpqMappings) {
        if (mapping.pView) UnmapViewOfFile(mapping.pView);
        if (mapping.hMapping) CloseHandle(mapping.hMapping);
        if (mapping.hFile != INVALID_HANDLE_VALUE) CloseHandle(mapping.hFile);
    }
    g_mpqMappings.clear();

    // Free cache
    {
        std::lock_guard<std::mutex> lock(g_vfsCacheMutex);
        g_vfsCache.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_handlesMutex);
        for (auto& pair : g_virtualHandles) {
            delete pair.second;
        }
        g_virtualHandles.clear();
    }

    g_initialized = false;
}

void OnFrame() {}

Stats GetStats() {
    Stats s;
    s.filesCached = g_filesCached.load();
    s.cacheHits = g_cacheHits.load();
    s.cacheMisses = g_cacheMisses.load();
    s.totalLoadTimeMs = g_totalLoadTimeMs.load();

    std::lock_guard<std::mutex> lock(g_queueMutex);
    s.queueDepth = (long)g_preloadQueue.size();

    return s;
}

void QueueFilePreload(const std::string& filename) {
    if (!g_initialized) return;
    
    std::string normPath = NormalizePath(filename);
    {
        std::lock_guard<std::mutex> lock(g_vfsCacheMutex);
        if (g_vfsCache.count(normPath)) return;
    }

    std::lock_guard<std::mutex> lock(g_queueMutex);
    g_preloadQueue.push(filename);
    g_queueCv.notify_one();
}

bool GetCachedFileData(const std::string& path, std::vector<uint8_t>& outData) {
    std::string normPath = NormalizePath(path);
    std::lock_guard<std::mutex> lock(g_vfsCacheMutex);
    auto it = g_vfsCache.find(normPath);
    if (it != g_vfsCache.end()) {
        outData = it->second;
        return true;
    }
    return false;
}

void AddCachedFile(const std::string& path, std::vector<uint8_t>&& data) {
    std::string normPath = NormalizePath(path);
    std::lock_guard<std::mutex> lock(g_vfsCacheMutex);
    g_vfsCache[normPath] = std::move(data);
    g_filesCached++;
}

} // namespace MpqMmapVfs
