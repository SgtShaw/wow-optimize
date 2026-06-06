// SavedVariables Async Writer
// Hooks the SV write path to offload file I/O to a background thread.
// WoW's SaveAccountData/SaveCharacterData blocks the main thread while
// serializing large addon tables (ElvUI, WeakAuras can be 10+ MB).

#include "version.h"
#include "MinHook.h"
#include "crash_dumper.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

extern "C" void Log(const char* fmt, ...);

// Background writer state
static HANDLE s_writerThread = NULL;
static volatile bool s_shutdown = false;
static CRITICAL_SECTION s_writeLock;
static volatile LONG s_pendingWrites = 0;
static volatile LONG64 s_totalBytesWritten = 0;
static volatile LONG s_writeCount = 0;

// WriteFile hook - intercept SV writes and offload to background
typedef BOOL (WINAPI* WriteFile_fn)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
static WriteFile_fn orig_WriteFile_SV = nullptr;

// Track which handles are SV files (WTF/Account/*.lua or WTF/SavedVariables/*.lua)
static HANDLE s_svHandles[32] = {};
static int s_svHandleCount = 0;
static SRWLOCK s_svHandleLock = SRWLOCK_INIT;

static bool IsSVHandle(HANDLE h) {
    AcquireSRWLockShared(&s_svHandleLock);
    for (int i = 0; i < s_svHandleCount; i++) {
        if (s_svHandles[i] == h) {
            ReleaseSRWLockShared(&s_svHandleLock);
            return true;
        }
    }
    ReleaseSRWLockShared(&s_svHandleLock);
    return false;
}

static void TrackSVHandle(HANDLE h) {
    AcquireSRWLockExclusive(&s_svHandleLock);
    if (s_svHandleCount < 32) {
        // Check not already tracked
        for (int i = 0; i < s_svHandleCount; i++) {
            if (s_svHandles[i] == h) {
                ReleaseSRWLockExclusive(&s_svHandleLock);
                return;
            }
        }
        s_svHandles[s_svHandleCount++] = h;
    }
    ReleaseSRWLockExclusive(&s_svHandleLock);
}

// CreateFileA hook to detect SV file opens
typedef HANDLE (WINAPI* CreateFileA_SV_fn)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static CreateFileA_SV_fn orig_CreateFileA_SV = nullptr;

static HANDLE WINAPI Hooked_CreateFileA_SV(LPCSTR lpFileName, DWORD dwAccess, DWORD dwShare,
    LPSECURITY_ATTRIBUTES lpSA, DWORD dwDisposition, DWORD dwFlags, HANDLE hTemplate)
{
    HANDLE result = orig_CreateFileA_SV(lpFileName, dwAccess, dwShare, lpSA, dwDisposition, dwFlags, hTemplate);
    
    if (result != INVALID_HANDLE_VALUE && lpFileName && (dwAccess & GENERIC_WRITE)) {
        // Check if this is a SavedVariables or Account data file
        const char* svMarker = strstr(lpFileName, "SavedVariables");
        const char* wtfMarker = strstr(lpFileName, "WTF");
        const char* luaExt = strrchr(lpFileName, '.');
        
        if (wtfMarker && luaExt && _stricmp(luaExt, ".lua") == 0) {
            TrackSVHandle(result);
            CrashDumper::RecordHookCall("CreateFileA_SV", (uintptr_t)result);
        }
    }
    
    return result;
}

// Async write queue
struct AsyncWriteTask {
    HANDLE hFile;
    void* buffer;       // Allocated copy of data
    DWORD bytesToWrite;
    volatile LONG completed;
    DWORD bytesWritten;
};

static constexpr int WRITE_QUEUE_SIZE = 64;
static AsyncWriteTask s_writeQueue[WRITE_QUEUE_SIZE] = {};
static volatile LONG s_queueHead = 0;
static volatile LONG s_queueTail = 0;

static DWORD WINAPI SVWriterThreadProc(LPVOID) {
    while (!s_shutdown) {
        LONG head = InterlockedCompareExchange(&s_queueHead, 0, 0);
        LONG tail = InterlockedCompareExchange(&s_queueTail, 0, 0);
        
        if (head == tail) {
            Sleep(1);
            continue;
        }
        
        AsyncWriteTask& task = s_writeQueue[head % WRITE_QUEUE_SIZE];
        
        EnterCriticalSection(&s_writeLock);
        __try {
            DWORD written = 0;
            BOOL ok = orig_WriteFile_SV(task.hFile, task.buffer, task.bytesToWrite, &written, NULL);
            task.bytesWritten = written;
            InterlockedExchange(&task.completed, ok ? 1 : -1);
            InterlockedAdd64(&s_totalBytesWritten, written);
            InterlockedIncrement(&s_writeCount);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            InterlockedExchange(&task.completed, -1);
            CrashDumper::FeatureError("SavedVarsAsync", "write exception");
        }
        LeaveCriticalSection(&s_writeLock);
        
        // Free the buffer copy
        if (task.buffer) {
            HeapFree(GetProcessHeap(), 0, task.buffer);
            task.buffer = NULL;
        }
        
        InterlockedIncrement(&s_queueHead);
        InterlockedDecrement(&s_pendingWrites);
    }
    return 0;
}

// WriteFile hook for SV handles
static BOOL WINAPI Hooked_WriteFile_SV(HANDLE hFile, LPCVOID lpBuffer, DWORD nBytesToWrite,
    LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)
{
    // Only intercept SV file writes (not overlapped, must be SV handle)
    if (!lpOverlapped && IsSVHandle(hFile) && nBytesToWrite > 0 && nBytesToWrite <= 64 * 1024 * 1024) {
        LONG tail = InterlockedIncrement(&s_queueTail) - 1;
        LONG head = InterlockedCompareExchange(&s_queueHead, 0, 0);
        
        if (tail - head < WRITE_QUEUE_SIZE) {
            AsyncWriteTask& task = s_writeQueue[tail % WRITE_QUEUE_SIZE];
            
            // Allocate and copy buffer (original may be freed after return)
            task.buffer = HeapAlloc(GetProcessHeap(), 0, nBytesToWrite);
            if (task.buffer) {
                memcpy(task.buffer, lpBuffer, nBytesToWrite);
                task.hFile = hFile;
                task.bytesToWrite = nBytesToWrite;
                task.completed = 0;
                task.bytesWritten = 0;
                
                InterlockedIncrement(&s_pendingWrites);
                CrashDumper::FeatureCall("SavedVarsAsync");
                
                // Report bytes written immediately (caller expects synchronous behavior)
                if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = nBytesToWrite;
                return TRUE;
            }
            // Allocation failed - fall through to sync write
            InterlockedDecrement(&s_queueTail);
        } else {
            // Queue full - fall through to sync write
            InterlockedDecrement(&s_queueTail);
        }
    }
    
    return orig_WriteFile_SV(hFile, lpBuffer, nBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);
}

bool InstallSavedVarsAsync() {
    InitializeCriticalSection(&s_writeLock);
    
    // Hook CreateFileA to track SV file handles
    HMODULE hK32 = GetModuleHandleA("kernel32.dll");
    if (!hK32) return false;
    
    void* pCF = (void*)GetProcAddress(hK32, "CreateFileA");
    void* pWF = (void*)GetProcAddress(hK32, "WriteFile");
    if (!pCF || !pWF) return false;
    
    if (MH_CreateHook(pCF, (void*)Hooked_CreateFileA_SV, (void**)&orig_CreateFileA_SV) != MH_OK) {
        Log("[SavedVarsAsync] CreateFileA hook FAILED");
        return false;
    }
    if (MH_EnableHook(pCF) != MH_OK) return false;
    
    if (MH_CreateHook(pWF, (void*)Hooked_WriteFile_SV, (void**)&orig_WriteFile_SV) != MH_OK) {
        Log("[SavedVarsAsync] WriteFile hook FAILED");
        return false;
    }
    if (MH_EnableHook(pWF) != MH_OK) return false;
    
    // Start writer thread
    s_shutdown = false;
    s_writerThread = CreateThread(NULL, 0, SVWriterThreadProc, NULL, 0, NULL);
    if (!s_writerThread) {
        Log("[SavedVarsAsync] Writer thread FAILED");
        return false;
    }
    SetThreadPriority(s_writerThread, THREAD_PRIORITY_BELOW_NORMAL);
    
    CrashDumper::RegisterFeature("SavedVarsAsync");
    Log("[SavedVarsAsync] ACTIVE (async SV writer, %d-slot queue)", WRITE_QUEUE_SIZE);
    return true;
}

void ShutdownSavedVarsAsync() {
    // Wait for pending writes to complete
    int waitMs = 0;
    while (InterlockedCompareExchange(&s_pendingWrites, 0, 0) > 0 && waitMs < 5000) {
        Sleep(10);
        waitMs += 10;
    }
    
    s_shutdown = true;
    if (s_writerThread) {
        WaitForSingleObject(s_writerThread, 2000);
        CloseHandle(s_writerThread);
        s_writerThread = NULL;
    }
    
    DeleteCriticalSection(&s_writeLock);
    
    if (s_writeCount > 0) {
        Log("[SavedVarsAsync] Stats: %ld writes, %.1f MB total",
            s_writeCount, s_totalBytesWritten / (1024.0 * 1024.0));
    }
}
