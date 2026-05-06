// ================================================================
// Predictive MPQ Prefetcher — Implementation
// WoW 3.3.5a build 12340
// ================================================================

#include "mpq_prefetch.h"
#include "lua_optimize.h"
#include "MinHook.h"
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <unordered_set>
#include <string>
#include <windows.h>

extern "C" void Log(const char* fmt, ...);

static constexpr DWORD ZONE_ID_ADDR = 0x00C6E6F8;
// Internal SFileOpenFileEx function address in Wow.exe (build 12340)
static void* SFILE_OPEN_FILE_EX_ADDR = (void*)0x004609B0;

static int g_currentZone = 0;
static std::unordered_set<std::string> g_prefetchedFiles;
static SRWLOCK g_cacheLock = SRWLOCK_INIT;

static volatile LONG g_filesQueued = 0;
static volatile LONG g_filesCompleted = 0;
static volatile LONG g_cacheHits = 0;
static volatile LONG g_cacheMisses = 0;
static volatile LONG g_zoneTransitions = 0;

static HANDLE g_workerThread = NULL;
static volatile bool g_workerShutdown = false;
static HANDLE g_workerEvent = NULL;

static constexpr int QUEUE_SIZE = 1024;
struct PrefetchEntry {
    char filename[260];
    volatile LONG ready;
};
static PrefetchEntry g_queue[QUEUE_SIZE] = {};
static volatile LONG g_queueHead = 0;
static volatile LONG g_queueTail = 0;

static bool g_hookInstalled = false;

static bool IsReadable(uintptr_t addr) {
    if (addr == 0) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((void*)addr, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    return !(mbi.Protect & PAGE_NOACCESS) && !(mbi.Protect & PAGE_GUARD);
}

static bool IsHeavyFile(const char* filename) {
    if (!filename) return false;
    const char* ext = strrchr(filename, '.');
    if (!ext) return false;
    return (_stricmp(ext, ".adt") == 0 ||
            _stricmp(ext, ".wmo") == 0 ||
            _stricmp(ext, ".mdx") == 0 ||
            _stricmp(ext, ".m2") == 0 ||
            _stricmp(ext, ".blp") == 0);
}

static DWORD WINAPI WorkerThreadProc(LPVOID) {
    while (!g_workerShutdown) {
        // Pause during UI reload to prevent accessing stale lua_State
        if (LuaOpt::IsReloading()) {
            Sleep(1);
            continue;
        }

        WaitForSingleObject(g_workerEvent, 100);
        
        LONG head = g_queueHead;
        LONG tail = g_queueTail;
        while (head != tail) {
            int slot = head & (QUEUE_SIZE - 1);
            PrefetchEntry* entry = &g_queue[slot];
            if (entry->ready) {
                HANDLE hFile = CreateFileA(entry->filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                if (hFile != INVALID_HANDLE_VALUE) {
                    char buf[65536];
                    DWORD bytesRead;
                    while (ReadFile(hFile, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0) {}
                    CloseHandle(hFile);
                    InterlockedIncrement(&g_filesCompleted);
                }
                InterlockedExchange(&entry->ready, 0);
            }
            head = (head + 1) & 0x7FFFFFFF;
            InterlockedExchange(&g_queueHead, head);
        }
    }
    return 0;
}

static void QueuePrefetch(const char* filename) {
    if (!IsHeavyFile(filename)) return;

    AcquireSRWLockShared(&g_cacheLock);
    bool cached = (g_prefetchedFiles.find(filename) != g_prefetchedFiles.end());
    ReleaseSRWLockShared(&g_cacheLock);
    if (cached) {
        InterlockedIncrement(&g_cacheHits);
        return;
    }
    InterlockedIncrement(&g_cacheMisses);

    LONG tail = InterlockedIncrement(&g_queueTail) - 1;
    int slot = tail & (QUEUE_SIZE - 1);
    PrefetchEntry* entry = &g_queue[slot];
    if (!entry->ready) {
        strncpy_s(entry->filename, filename, _TRUNCATE);
        InterlockedExchange(&entry->ready, 1);
        InterlockedIncrement(&g_filesQueued);
        SetEvent(g_workerEvent);
        
        AcquireSRWLockExclusive(&g_cacheLock);
        g_prefetchedFiles.insert(filename);
        ReleaseSRWLockExclusive(&g_cacheLock);
    }
}

// Signature: int __cdecl sub_4609B0(int a1, int ArgList, int a3, int a4, _DWORD *a5)
typedef int (__cdecl *SFileOpenFileEx_fn)(int, int, int, int, DWORD*);
static SFileOpenFileEx_fn orig_SFileOpenFileEx = nullptr;

static int __cdecl Hooked_SFileOpenFileEx(int a1, int filenamePtr, int a3, int a4, DWORD* a5) {
    int result = orig_SFileOpenFileEx(a1, filenamePtr, a3, a4, a5);

    // Attempt to extract filename. In this function, filenamePtr often points to a structure
    // where the first 4 bytes may be a pointer to a string, or it is the pointer itself.
    // Check memory for safety.
    if (result && filenamePtr != 0) {
        __try {
            // Check if the argument itself is a string
            if (IsReadable(filenamePtr)) {
                const char* str = (const char*)filenamePtr;
                // Simple heuristic: if the first character is printable ASCII
                if (str[0] >= 32 && str[0] <= 126) {
                    QueuePrefetch(str);
                } else {
                    // Possibly a pointer to a pointer (CStoreItem structure or similar)
                    int potentialPtr = *(int*)filenamePtr;
                    if (potentialPtr != 0 && IsReadable(potentialPtr)) {
                        const char* innerStr = (const char*)potentialPtr;
                        if (innerStr[0] >= 32 && innerStr[0] <= 126) {
                            QueuePrefetch(innerStr);
                        }
                    }
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            // Ignore access errors
        }
    }

    return result;
}

namespace MPQPrefetch {

bool Init() {
    Log("[MPQPrefetch] Init");
    
    g_workerEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    g_workerShutdown = false;
    g_workerThread = CreateThread(NULL, 0, WorkerThreadProc, NULL, 0, NULL);
    if (g_workerThread) {
        SetThreadPriority(g_workerThread, THREAD_PRIORITY_LOWEST);
    }

    return true;
}

void Shutdown() {
    Log("[MPQPrefetch] Shutdown");
    g_workerShutdown = true;
    if (g_workerEvent) SetEvent(g_workerEvent);
    if (g_workerThread) {
        WaitForSingleObject(g_workerThread, 5000);
        CloseHandle(g_workerThread);
    }
    if (g_workerEvent) CloseHandle(g_workerEvent);
    
    if (g_hookInstalled) {
        MH_DisableHook(SFILE_OPEN_FILE_EX_ADDR);
        MH_RemoveHook(SFILE_OPEN_FILE_EX_ADDR);
        g_hookInstalled = false;
    }
    
    Log("[MPQPrefetch] Stats: Queued=%ld, Completed=%ld, Hits=%ld, Misses=%ld",
        g_filesQueued, g_filesCompleted, g_cacheHits, g_cacheMisses);
}

void ClearQueues() {
    // Reset queue indices to clear all pending prefetch requests
    InterlockedExchange(&g_queueHead, 0);
    InterlockedExchange(&g_queueTail, 0);

    // Reset ready flags on all entries
    for (int i = 0; i < QUEUE_SIZE; i++) {
        g_queue[i].ready = 0;
    }

    // Reset stats counters
    InterlockedExchange(&g_filesQueued, 0);
    InterlockedExchange(&g_filesCompleted, 0);
    InterlockedExchange(&g_cacheHits, 0);
    InterlockedExchange(&g_cacheMisses, 0);

    Log("[MPQPrefetch] Queues cleared (UI reload / character switch)");
}

void OnFrame(DWORD mainThreadId) {
    if (GetCurrentThreadId() != mainThreadId) return;

    // Lazy hook installation by hardcoded address
    if (!g_hookInstalled) {
        if (MH_CreateHook(SFILE_OPEN_FILE_EX_ADDR, (void*)Hooked_SFileOpenFileEx, (void**)&orig_SFileOpenFileEx) == MH_OK) {
            MH_EnableHook(SFILE_OPEN_FILE_EX_ADDR);
            Log("[MPQPrefetch] Hooked internal SFileOpenFileEx at 0x%p", SFILE_OPEN_FILE_EX_ADDR);
            g_hookInstalled = true;
        }
    }

    // Zone detection
    if (IsReadable(ZONE_ID_ADDR)) {
        int zoneId = *(int*)ZONE_ID_ADDR;
        if (zoneId != g_currentZone && zoneId > 0 && zoneId < 10000) {
            g_currentZone = zoneId;
            InterlockedIncrement(&g_zoneTransitions);
            Log("[MPQPrefetch] Zone changed: %d", g_currentZone);
        }
    }
}

Stats GetStats() {
    Stats s;
    s.filesQueued = g_filesQueued;
    s.filesCompleted = g_filesCompleted;
    s.cacheHits = g_cacheHits;
    s.cacheMisses = g_cacheMisses;
    s.zoneTransitions = g_zoneTransitions;
    s.queueDepth = g_queueTail - g_queueHead;
    s.totalPrefetchTimeMs = 0;
    return s;
}

} // namespace MPQPrefetch
