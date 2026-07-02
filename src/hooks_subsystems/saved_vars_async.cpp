// ============================================================================
// Module: saved_vars_async.cpp
// Description: Supporting utility functions for `saved_vars_async.cpp`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================

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

// CloseHandle hook - intercept handle closes to remove from active tracked list
typedef BOOL (WINAPI* CloseHandle_fn)(HANDLE);
static CloseHandle_fn orig_CloseHandle = nullptr;

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

void TrackSVHandle(HANDLE h) {
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

// Async write queue
struct AsyncWriteTask {
    HANDLE hFile;
    void* buffer;       // Allocated copy of data
    DWORD bytesToWrite;
    volatile LONG completed;
    DWORD bytesWritten;
    volatile LONG ready; // 0 = not ready, 1 = ready for writing
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
        
        // Spin-wait for the task to be marked ready by the calling thread
        while (InterlockedCompareExchange(&task.ready, 0, 0) == 0 && !s_shutdown) {
            Sleep(1);
        }
        if (s_shutdown) break;
        
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
        
        // Clean close the duplicated handle using orig_CloseHandle to bypass our detour
        if (task.hFile != INVALID_HANDLE_VALUE) {
            if (orig_CloseHandle) {
                orig_CloseHandle(task.hFile);
            } else {
                CloseHandle(task.hFile);
            }
            task.hFile = INVALID_HANDLE_VALUE;
        }
        LeaveCriticalSection(&s_writeLock);
        
        // Free the buffer copy
        if (task.buffer) {
            HeapFree(GetProcessHeap(), 0, task.buffer);
            task.buffer = NULL;
        }
        
        // Clear the ready flag so the slot can be reused
        InterlockedExchange(&task.ready, 0);
        
        InterlockedIncrement(&s_queueHead);
        InterlockedDecrement(&s_pendingWrites);
    }
    return 0;
}

// CloseHandle hook implementation

static BOOL WINAPI Hooked_CloseHandle(HANDLE hObject) {
    if (hObject != INVALID_HANDLE_VALUE) {
        AcquireSRWLockExclusive(&s_svHandleLock);
        for (int i = 0; i < s_svHandleCount; i++) {
            if (s_svHandles[i] == hObject) {
                // Remove from active list by shifting elements
                for (int j = i; j < s_svHandleCount - 1; j++) {
                    s_svHandles[j] = s_svHandles[j + 1];
                }
                s_svHandles[--s_svHandleCount] = nullptr;
                break;
            }
        }
        ReleaseSRWLockExclusive(&s_svHandleLock);
    }
    return orig_CloseHandle(hObject);
}

// WriteFile hook for SV handles
static BOOL WINAPI Hooked_WriteFile_SV(HANDLE hFile, LPCVOID lpBuffer, DWORD nBytesToWrite,
    LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)
{
    // Only intercept SV file writes (not overlapped, must be SV handle)
    if (!lpOverlapped && IsSVHandle(hFile) && nBytesToWrite > 0 && nBytesToWrite <= 64 * 1024 * 1024) {
        LONG tail = InterlockedCompareExchange(&s_queueTail, 0, 0);
        LONG head = InterlockedCompareExchange(&s_queueHead, 0, 0);
        
        // Congestion control: block/spin-wait if the queue is full to prevent out-of-order writes
        while (tail - head >= WRITE_QUEUE_SIZE) {
            Sleep(1);
            head = InterlockedCompareExchange(&s_queueHead, 0, 0);
        }
        
        tail = InterlockedIncrement(&s_queueTail) - 1;
        AsyncWriteTask& task = s_writeQueue[tail % WRITE_QUEUE_SIZE];
        
        // Spin-wait if the target slot is currently recycling
        while (InterlockedCompareExchange(&task.ready, 0, 0) != 0) {
            Sleep(1);
        }
        
        // Allocate and copy buffer (original may be freed after return)
        task.buffer = HeapAlloc(GetProcessHeap(), 0, nBytesToWrite);
        if (task.buffer) {
            HANDLE dupHandle = INVALID_HANDLE_VALUE;
            if (DuplicateHandle(GetCurrentProcess(), hFile, GetCurrentProcess(), &dupHandle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
                memcpy(task.buffer, lpBuffer, nBytesToWrite);
                task.hFile = dupHandle;
                task.bytesToWrite = nBytesToWrite;
                task.completed = 0;
                task.bytesWritten = 0;
                
                InterlockedIncrement(&s_pendingWrites);
                CrashDumper::FeatureCall("SavedVarsAsync");
                
                // Mark slot as ready for consumption
                InterlockedExchange(&task.ready, 1);
                
                // Report bytes written immediately (caller expects synchronous behavior)
                if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = nBytesToWrite;
                return TRUE;
            } else {
                HeapFree(GetProcessHeap(), 0, task.buffer);
                task.buffer = nullptr;
            }
        }
        // Allocation or duplication failed - fall back to sync write
        InterlockedDecrement(&s_queueTail);
    }
    
    return orig_WriteFile_SV(hFile, lpBuffer, nBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);
}

bool InstallSavedVarsAsync() {
    InitializeCriticalSection(&s_writeLock);
    
    HMODULE hK32 = GetModuleHandleA("kernel32.dll");
    if (!hK32) return false;
    
    void* pWF = (void*)GetProcAddress(hK32, "WriteFile");
    if (!pWF) return false;
    
    if (MH_CreateHook(pWF, (void*)Hooked_WriteFile_SV, (void**)&orig_WriteFile_SV) != MH_OK) {
        Log("[SavedVarsAsync] WriteFile hook FAILED");
        return false;
    }
    if (MH_EnableHook(pWF) != MH_OK) return false;

    void* pCH = (void*)GetProcAddress(hK32, "CloseHandle");
    if (pCH) {
        if (MH_CreateHook(pCH, (void*)Hooked_CloseHandle, (void**)&orig_CloseHandle) == MH_OK) {
            MH_EnableHook(pCH);
        }
    }
    
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
    
    if (orig_CloseHandle) {
        void* pCH = (void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CloseHandle");
        if (pCH) MH_DisableHook(pCH);
    }
    
    DeleteCriticalSection(&s_writeLock);
    
    if (s_writeCount > 0) {
        Log("[SavedVarsAsync] Stats: %ld writes, %.1f MB total",
            s_writeCount, s_totalBytesWritten / (1024.0 * 1024.0));
    }
}

void FlushSavedVarsAsyncSynchronously() {
    LONG head = s_queueHead;
    LONG tail = s_queueTail;
    if (head == tail) return;

    Log("[SavedVarsAsync] Flushing %ld pending writes synchronously on process exit...", tail - head);

    while (head != tail) {
        AsyncWriteTask& task = s_writeQueue[head % WRITE_QUEUE_SIZE];
        
        // Wait briefly if the task is not yet ready (the caller thread is copying the buffer)
        int spins = 0;
        while (InterlockedCompareExchange(&task.ready, 0, 0) == 0 && spins < 100) {
            Sleep(1);
            spins++;
        }
        
        if (task.hFile != INVALID_HANDLE_VALUE && task.buffer && task.bytesToWrite > 0) {
            DWORD written = 0;
            if (orig_WriteFile_SV) {
                orig_WriteFile_SV(task.hFile, task.buffer, task.bytesToWrite, &written, NULL);
            } else {
                WriteFile(task.hFile, task.buffer, task.bytesToWrite, &written, NULL);
            }
            
            if (task.hFile != INVALID_HANDLE_VALUE) {
                if (orig_CloseHandle) {
                    orig_CloseHandle(task.hFile);
                } else {
                    CloseHandle(task.hFile);
                }
                task.hFile = INVALID_HANDLE_VALUE;
            }
        }
        
        if (task.buffer) {
            HeapFree(GetProcessHeap(), 0, task.buffer);
            task.buffer = NULL;
        }
        
        InterlockedExchange(&task.ready, 0);
        head++;
    }
    s_queueHead = head;
    s_pendingWrites = 0;
    Log("[SavedVarsAsync] Synchronous flush complete.");
}
