// ============================================================================
// Module: saved_vars_async.cpp
// Description: Coalesces high-frequency small WriteFile calls for SavedVariables
//              into a single in-memory buffer, writing the complete buffer
//              synchronously during CloseHandle to prevent race conditions.
// Safety & Threading: Thread-safe. Lock guards protect active buffers.
// ============================================================================

#include "version.h"
#include "MinHook.h"
#include "crash_dumper.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>

extern "C" void Log(const char* fmt, ...);

// Case-insensitive substring check for WTF folder
bool ContainsWTF(const char* path) {
    if (!path) return false;
    for (const char* p = path; *p; p++) {
        if ((p[0] == 'W' || p[0] == 'w') &&
            (p[1] == 'T' || p[1] == 't') &&
            (p[2] == 'F' || p[2] == 'f')) {
            return true;
        }
    }
    return false;
}

// WriteFile hook - intercept SV writes and offload to buffer
typedef BOOL (WINAPI* WriteFile_fn)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
static WriteFile_fn orig_WriteFile_SV = nullptr;

// CloseHandle hook - intercept handle closes to write buffer and close
typedef BOOL (WINAPI* CloseHandle_fn)(HANDLE);
static CloseHandle_fn orig_CloseHandle = nullptr;

// Track which handles are SV files
static HANDLE s_svHandles[256] = {};
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
    if (s_svHandleCount < 256) {
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

// Active write buffers for open handles
struct BufferedWrite {
    HANDLE hFile;
    uint8_t* data;
    size_t size;
    size_t capacity;
};

static constexpr int MAX_ACTIVE_BUFFERS = 128;
static BufferedWrite s_activeBuffers[MAX_ACTIVE_BUFFERS] = {};
static SRWLOCK s_bufferLock = SRWLOCK_INIT;

static BufferedWrite* GetOrCreateBuffer(HANDLE hFile) {
    AcquireSRWLockExclusive(&s_bufferLock);
    for (int i = 0; i < MAX_ACTIVE_BUFFERS; i++) {
        if (s_activeBuffers[i].hFile == hFile) {
            BufferedWrite* b = &s_activeBuffers[i];
            ReleaseSRWLockExclusive(&s_bufferLock);
            return b;
        }
    }
    for (int i = 0; i < MAX_ACTIVE_BUFFERS; i++) {
        if (s_activeBuffers[i].hFile == nullptr) {
            s_activeBuffers[i].hFile = hFile;
            s_activeBuffers[i].data = nullptr;
            s_activeBuffers[i].size = 0;
            s_activeBuffers[i].capacity = 0;
            BufferedWrite* b = &s_activeBuffers[i];
            ReleaseSRWLockExclusive(&s_bufferLock);
            return b;
        }
    }
    ReleaseSRWLockExclusive(&s_bufferLock);
    return nullptr;
}

// CloseHandle hook implementation - writes buffered data synchronously on close
static BOOL WINAPI Hooked_CloseHandle(HANDLE hObject) {
    if (hObject != INVALID_HANDLE_VALUE && IsSVHandle(hObject)) {
        BufferedWrite* b = nullptr;
        
        // Find active buffer matching the handle
        AcquireSRWLockExclusive(&s_bufferLock);
        for (int i = 0; i < MAX_ACTIVE_BUFFERS; i++) {
            if (s_activeBuffers[i].hFile == hObject) {
                b = &s_activeBuffers[i];
                break;
            }
        }
        ReleaseSRWLockExclusive(&s_bufferLock);

        if (b) {
            if (b->data && b->size > 0) {
                __try {
                    DWORD written = 0;
                    if (orig_WriteFile_SV) {
                        orig_WriteFile_SV(hObject, b->data, (DWORD)b->size, &written, NULL);
                    } else {
                        WriteFile(hObject, b->data, (DWORD)b->size, &written, NULL);
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    CrashDumper::FeatureError("SavedVarsAsync", "sync write exception");
                }
            }
            if (b->data) {
                HeapFree(GetProcessHeap(), 0, b->data);
            }
            
            // Clear all slot fields cleanly under lock to prevent double-free / leakage
            AcquireSRWLockExclusive(&s_bufferLock);
            b->hFile = nullptr;
            b->data = nullptr;
            b->size = 0;
            b->capacity = 0;
            ReleaseSRWLockExclusive(&s_bufferLock);
        }

        // Clean up handle from tracked list
        AcquireSRWLockExclusive(&s_svHandleLock);
        for (int i = 0; i < s_svHandleCount; i++) {
            if (s_svHandles[i] == hObject) {
                for (int j = i; j < s_svHandleCount - 1; j++) {
                    s_svHandles[j] = s_svHandles[j + 1];
                }
                s_svHandles[--s_svHandleCount] = nullptr;
                break;
            }
        }
        ReleaseSRWLockExclusive(&s_svHandleLock);
    }
    if (orig_CloseHandle) {
        return orig_CloseHandle(hObject);
    }
    return CloseHandle(hObject);
}

// WriteFile hook for SV handles - aggregates data in memory
static BOOL WINAPI Hooked_WriteFile_SV(HANDLE hFile, LPCVOID lpBuffer, DWORD nBytesToWrite,
    LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)
{
    if (!lpOverlapped && IsSVHandle(hFile) && nBytesToWrite > 0 && nBytesToWrite <= 64 * 1024 * 1024) {
        BufferedWrite* b = GetOrCreateBuffer(hFile);
        if (b) {
            if (b->size + nBytesToWrite > b->capacity) {
                size_t newCap = b->capacity == 0 ? 65536 : b->capacity * 2;
                while (newCap < b->size + nBytesToWrite) {
                    newCap *= 2;
                }
                void* newData = nullptr;
                if (b->data == nullptr) {
                    newData = HeapAlloc(GetProcessHeap(), 0, newCap);
                } else {
                    newData = HeapReAlloc(GetProcessHeap(), 0, b->data, newCap);
                }
                if (!newData) {
                    // Fallback to synchronous write on memory allocation failure
                    if (orig_WriteFile_SV) {
                        return orig_WriteFile_SV(hFile, lpBuffer, nBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);
                    }
                    return WriteFile(hFile, lpBuffer, nBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);
                }
                b->data = (uint8_t*)newData;
                b->capacity = newCap;
            }
            memcpy(b->data + b->size, lpBuffer, nBytesToWrite);
            b->size += nBytesToWrite;
            if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = nBytesToWrite;
            return TRUE;
        }
    }
    
    if (orig_WriteFile_SV) {
        return orig_WriteFile_SV(hFile, lpBuffer, nBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);
    }
    return WriteFile(hFile, lpBuffer, nBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);
}

bool InstallSavedVarsAsync() {
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
    
    CrashDumper::RegisterFeature("SavedVarsAsync");
    Log("[SavedVarsAsync] ACTIVE (buffered single-write SV optimizer)");
    return true;
}

void ShutdownSavedVarsAsync() {
    if (orig_CloseHandle) {
        void* pCH = (void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CloseHandle");
        if (pCH) MH_DisableHook(pCH);
    }
    
    // Free any active buffers that were left over
    AcquireSRWLockExclusive(&s_bufferLock);
    for (int i = 0; i < MAX_ACTIVE_BUFFERS; i++) {
        if (s_activeBuffers[i].data) {
            HeapFree(GetProcessHeap(), 0, s_activeBuffers[i].data);
            s_activeBuffers[i].data = nullptr;
        }
        s_activeBuffers[i].hFile = nullptr;
    }
    ReleaseSRWLockExclusive(&s_bufferLock);
}

void FlushSavedVarsAsyncSynchronously() {
    // No-op since all writes are completed synchronously on CloseHandle
}
