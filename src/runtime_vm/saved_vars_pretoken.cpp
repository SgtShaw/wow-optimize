#include "saved_vars_pretoken.h"
#include "version.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <thread>
#include <atomic>

extern "C" void Log(const char* fmt, ...);

namespace SavedVarsPretoken {

static std::unordered_map<std::string, std::vector<uint8_t>> g_cache;
static SRWLOCK g_cacheLock = SRWLOCK_INIT;
static std::atomic<bool> g_ready{false};

static constexpr int MAX_HANDLES = 128;
static HANDLE g_handles[MAX_HANDLES] = {};
static std::string g_handlePaths[MAX_HANDLES];
static int g_handleCount = 0;
static SRWLOCK g_handleLock = SRWLOCK_INIT;

static std::vector<std::string> g_fileList;
static std::thread g_workerThread;
static std::atomic<bool> g_shutdown{false};

static std::string MinifyLua(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    bool inComment = false;
    bool inString = false;
    char stringChar = 0;
    
    for (size_t i = 0; i < input.size(); i++) {
        char c = input[i];
        if (inComment) {
            if (c == '\n' || c == '\r') {
                inComment = false;
                output.push_back('\n');
            }
            continue;
        }
        if (inString) {
            output.push_back(c);
            if (c == stringChar) {
                bool escaped = false;
                for (size_t j = i - 1; j > 0; j--) {
                    if (input[j] == '\\') escaped = !escaped;
                    else break;
                }
                if (!escaped) {
                    inString = false;
                }
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            inString = true;
            stringChar = c;
            output.push_back(c);
            continue;
        }
        if (c == '-' && i + 1 < input.size() && input[i + 1] == '-') {
            inComment = true;
            i++;
            continue;
        }
        if (c == ' ' || c == '\t') {
            if (!output.empty() && output.back() != ' ' && output.back() != '\n') {
                output.push_back(' ');
            }
            continue;
        }
        if (c == '\n' || c == '\r') {
            if (!output.empty() && output.back() != '\n') {
                output.push_back('\n');
            }
            while (i + 1 < input.size() && (input[i + 1] == '\n' || input[i + 1] == '\r')) {
                i++;
            }
            continue;
        }
        output.push_back(c);
    }
    return output;
}

static void WorkerProc() {
    for (size_t i = 0; i < g_fileList.size() && !g_shutdown.load(); i++) {
        std::string path = g_fileList[i];
        HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;
        
        DWORD size = GetFileSize(h, NULL);
        if (size > 0 && size < 64 * 1024 * 1024) {
            std::vector<uint8_t> data(size);
            DWORD read = 0;
            if (ReadFile(h, data.data(), size, &read, NULL) && read == size) {
                AcquireSRWLockExclusive(&g_cacheLock);
                g_cache[path] = std::move(data);
                ReleaseSRWLockExclusive(&g_cacheLock);
            }
        }
        CloseHandle(h);
    }
    g_ready.store(true);
    Log("[SavedVarsPretoken] Preloading complete. Cached %d files.", (int)g_cache.size());
}

static void ScanSavedVarsDir(const std::string& base, int depth) {
    if (depth > 6) return;
    WIN32_FIND_DATAA fd;
    std::string search = base + "\\*";
    HANDLE h = FindFirstFileA(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.') continue;
        std::string full = base + "\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ScanSavedVarsDir(full, depth + 1);
        } else {
            const char* ext = strrchr(fd.cFileName, '.');
            if (ext && _stricmp(ext, ".lua") == 0) {
                if (full.find("SavedVariables") != std::string::npos) {
                    g_fileList.push_back(full);
                }
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

void OnCreateFile(HANDLE hFile, const char* filename, DWORD dwAccess) {
    if (!hFile || hFile == INVALID_HANDLE_VALUE || !filename) return;
    
    std::string normPath(filename);
    for (char& c : normPath) {
        if (c == '/') c = '\\';
    }
    
    if (dwAccess & (GENERIC_WRITE | FILE_WRITE_DATA | GENERIC_ALL)) {
        AcquireSRWLockExclusive(&g_cacheLock);
        g_cache.erase(normPath);
        ReleaseSRWLockExclusive(&g_cacheLock);
        return;
    }
    
    AcquireSRWLockShared(&g_cacheLock);
    bool cached = (g_cache.find(normPath) != g_cache.end());
    ReleaseSRWLockShared(&g_cacheLock);
    if (!cached) return;
    
    AcquireSRWLockExclusive(&g_handleLock);
    if (g_handleCount < MAX_HANDLES) {
        g_handles[g_handleCount] = hFile;
        g_handlePaths[g_handleCount] = normPath;
        g_handleCount++;
    }
    ReleaseSRWLockExclusive(&g_handleLock);
}

void OnCloseHandle(HANDLE hFile) {
    if (!hFile || hFile == INVALID_HANDLE_VALUE) return;
    
    AcquireSRWLockExclusive(&g_handleLock);
    for (int i = 0; i < g_handleCount; i++) {
        if (g_handles[i] == hFile) {
            g_handles[i] = g_handles[g_handleCount - 1];
            g_handlePaths[i].swap(g_handlePaths[g_handleCount - 1]);
            g_handles[g_handleCount - 1] = NULL;
            g_handlePaths[g_handleCount - 1].clear();
            g_handleCount--;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_handleLock);
}

bool TryServe(HANDLE hFile, LPVOID lpBuffer, DWORD nBytes, LPDWORD lpBytesRead) {
    AcquireSRWLockShared(&g_handleLock);
    for (int i = 0; i < g_handleCount; i++) {
        if (g_handles[i] == hFile) {
            std::string path = g_handlePaths[i];
            ReleaseSRWLockShared(&g_handleLock);
            
            AcquireSRWLockShared(&g_cacheLock);
            auto it = g_cache.find(path);
            if (it != g_cache.end() && !it->second.empty()) {
                const auto& data = it->second;
                DWORD filePos = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
                if (filePos != INVALID_SET_FILE_POINTER && filePos < (DWORD)data.size()) {
                    DWORD toCopy = nBytes;
                    if (filePos + nBytes > (DWORD)data.size()) {
                        toCopy = (DWORD)data.size() - filePos;
                    }
                    memcpy(lpBuffer, data.data() + filePos, toCopy);
                    if (lpBytesRead) *lpBytesRead = toCopy;
                    SetFilePointer(hFile, filePos + toCopy, NULL, FILE_BEGIN);
                    ReleaseSRWLockShared(&g_cacheLock);
                    return true;
                }
            }
            ReleaseSRWLockShared(&g_cacheLock);
            return false;
        }
    }
    ReleaseSRWLockShared(&g_handleLock);
    return false;
}

bool GetMinifiedFileSize(HANDLE hFile, PLARGE_INTEGER lpFileSize) {
    if (!hFile || hFile == INVALID_HANDLE_VALUE || !lpFileSize) return false;
    
    AcquireSRWLockShared(&g_handleLock);
    for (int i = 0; i < g_handleCount; i++) {
        if (g_handles[i] == hFile) {
            std::string path = g_handlePaths[i];
            ReleaseSRWLockShared(&g_handleLock);
            
            AcquireSRWLockShared(&g_cacheLock);
            auto it = g_cache.find(path);
            if (it != g_cache.end() && !it->second.empty()) {
                lpFileSize->QuadPart = it->second.size();
                ReleaseSRWLockShared(&g_cacheLock);
                return true;
            }
            ReleaseSRWLockShared(&g_cacheLock);
            return false;
        }
    }
    ReleaseSRWLockShared(&g_handleLock);
    return false;
}

bool Init() {
    #if TEST_DISABLE_SAVED_VARS_PRETOKEN
    return true;
    #endif
    
    g_shutdown = false;
    g_workerThread = std::thread([]() {
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        char* lastSep = strrchr(exePath, '\\');
        if (lastSep) *lastSep = 0;
        
        std::string wtfDir = std::string(exePath) + "\\WTF";
        ScanSavedVarsDir(wtfDir, 0);
        
        Log("[SavedVarsPretoken] Found %d SavedVariables files to preload & minify.", (int)g_fileList.size());
        if (g_fileList.empty()) {
            g_ready.store(true);
            return;
        }
        WorkerProc();
    });
    return true;
}

void Shutdown() {
    g_shutdown = true;
    if (g_workerThread.joinable()) {
        g_workerThread.join();
    }
    g_cache.clear();
}

} // namespace SavedVarsPretoken
