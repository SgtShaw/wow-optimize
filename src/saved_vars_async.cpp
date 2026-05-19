#include "saved_vars_async.h"
#include "version.h"
#include "MinHook.h"
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <cstring>

extern "C" void Log(const char* fmt, ...);

namespace SavedVarsAsync {

typedef BOOL (WINAPI* WriteFile_fn)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL (WINAPI* CloseHandle_fn)(HANDLE);

static WriteFile_fn   orig_WriteFile   = nullptr;
static CloseHandle_fn orig_CloseHandle = nullptr;

static volatile LONG g_active        = 0;
static HANDLE        g_workerThread  = nullptr;
static HANDLE        g_wakeupEvent   = nullptr;
static HANDLE        g_drainEvent    = nullptr;
static volatile LONG g_stopRequested = 0;

// Per-handle classification: 0=unknown, 1=savedvars, 2=other (skip).
static std::unordered_map<HANDLE, uint8_t> g_handleClass;
static std::mutex                          g_handleMutex;

struct Entry {
    HANDLE        hFile;
    void*         buffer;
    DWORD         bytes;
    volatile LONG ready;
};
static const size_t  QUEUE_SIZE = 256;
static const size_t  QUEUE_MASK = QUEUE_SIZE - 1;
static Entry         g_queue[QUEUE_SIZE] = {};
static volatile LONG g_queueHead = 0;
static volatile LONG g_queueTail = 0;

static volatile LONG64 g_writesAsync   = 0;
static volatile LONG64 g_writesSync    = 0;
static volatile LONG64 g_bytesAsync    = 0;
static volatile LONG64 g_writeFailures = 0;

static bool PathLooksLikeSV(const wchar_t* p, size_t n) {
    if (n < 16) return false;
    for (size_t i = 0; i + 15 <= n; i++) {
        const wchar_t* q = p + i;
        if ((q[0]==L'S'||q[0]==L's') && (q[1]==L'a'||q[1]==L'A') &&
           (q[2]==L'v'||q[2]==L'V') && (q[3]==L'e'||q[3]==L'E') &&
           (q[4]==L'd'||q[4]==L'D') &&
           (q[5]==L'V'||q[5]==L'v') && (q[6]==L'a'||q[6]==L'A') &&
           (q[7]==L'r'||q[7]==L'R') && (q[8]==L'i'||q[8]==L'I') &&
           (q[9]==L'a'||q[9]==L'A') && (q[10]==L'b'||q[10]==L'B') &&
           (q[11]==L'l'||q[11]==L'L') && (q[12]==L'e'||q[12]==L'E') &&
           (q[13]==L's'||q[13]==L'S') &&
           (q[14]==L'\\'||q[14]==L'/'))
        {
            return true;
        }
    }
    return false;
}

// Lazy classification: GetFinalPathNameByHandleW on first write. Result cached.
static uint8_t ClassifyHandle(HANDLE h) {
    {
        std::lock_guard<std::mutex> g(g_handleMutex);
        auto it = g_handleClass.find(h);
        if (it != g_handleClass.end()) return it->second;
    }
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetFinalPathNameByHandleW(h, buf, _countof(buf), FILE_NAME_NORMALIZED);
    uint8_t cls = (n > 0 && n < _countof(buf) && PathLooksLikeSV(buf, n)) ? 1 : 2;
    {
        std::lock_guard<std::mutex> g(g_handleMutex);
        g_handleClass[h] = cls;
    }
    return cls;
}

static bool TryEnqueue(HANDLE h, LPCVOID buf, DWORD n) {
    LONG head = g_queueHead;
    LONG nextHead = (head + 1) & (LONG)QUEUE_MASK;
    if (nextHead == g_queueTail) return false;

    void* copy = HeapAlloc(GetProcessHeap(), 0, n);
    if (!copy) return false;
    memcpy(copy, buf, n);

    g_queue[head].hFile  = h;
    g_queue[head].buffer = copy;
    g_queue[head].bytes  = n;
    InterlockedExchange(&g_queue[head].ready, 1);
    InterlockedExchange(&g_queueHead, nextHead);
    SetEvent(g_wakeupEvent);
    return true;
}

static BOOL WINAPI Hook_WriteFile(HANDLE h, LPCVOID buf, DWORD n,
                                  LPDWORD pWritten, LPOVERLAPPED ov) {
    if (!g_active || ov || n == 0) {
        return orig_WriteFile(h, buf, n, pWritten, ov);
    }
    if (ClassifyHandle(h) != 1) {
        return orig_WriteFile(h, buf, n, pWritten, ov);
    }
    if (TryEnqueue(h, buf, n)) {
        if (pWritten) *pWritten = n;
        InterlockedIncrement64(&g_writesAsync);
        InterlockedExchangeAdd64(&g_bytesAsync, n);
        return TRUE;
    }
    InterlockedIncrement64(&g_writesSync);
    return orig_WriteFile(h, buf, n, pWritten, ov);
}

static BOOL WINAPI Hook_CloseHandle(HANDLE h) {
    {
        std::lock_guard<std::mutex> g(g_handleMutex);
        g_handleClass.erase(h);
    }
    return orig_CloseHandle(h);
}

static DWORD WINAPI WorkerThread(LPVOID) {
    while (!g_stopRequested) {
        WaitForSingleObject(g_wakeupEvent, 100);
        for (;;) {
            LONG tail = g_queueTail;
            if (tail == g_queueHead) break;
            if (!g_queue[tail].ready) break;

            HANDLE h = g_queue[tail].hFile;
            void*  b = g_queue[tail].buffer;
            DWORD  n = g_queue[tail].bytes;

            DWORD wrote = 0;
            BOOL ok = orig_WriteFile ? orig_WriteFile(h, b, n, &wrote, NULL) : FALSE;
            if (!ok || wrote != n) InterlockedIncrement64(&g_writeFailures);

            HeapFree(GetProcessHeap(), 0, b);
            g_queue[tail].buffer = nullptr;
            InterlockedExchange(&g_queue[tail].ready, 0);
            InterlockedExchange(&g_queueTail, (tail + 1) & (LONG)QUEUE_MASK);
        }
        SetEvent(g_drainEvent);
    }
    return 0;
}

bool Init() {
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (!k32) return false;

    // CreateFileA/W are hooked by dllmain.cpp's MPQ tracker; skip them
    // and classify lazily in WriteFile via GetFinalPathNameByHandleW.
    void* pWrite = (void*)GetProcAddress(k32, "WriteFile");
    void* pClose = (void*)GetProcAddress(k32, "CloseHandle");
    if (!pWrite || !pClose) return false;

    g_wakeupEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
    g_drainEvent  = CreateEventA(NULL, TRUE,  FALSE, NULL);
    if (!g_wakeupEvent || !g_drainEvent) return false;

    if (MH_CreateHook(pWrite, (void*)Hook_WriteFile, (void**)&orig_WriteFile) != MH_OK) {
        Log("[SavedVarsAsync] WriteFile hook FAILED");
        return false;
    }
    MH_EnableHook(pWrite);

    if (MH_CreateHook(pClose, (void*)Hook_CloseHandle, (void**)&orig_CloseHandle) == MH_OK) {
        MH_EnableHook(pClose);
    }
    // CloseHandle hook is best-effort; if it fails we just leak handle-class
    // entries until the map is cleared on shutdown. Not fatal.

    g_workerThread = CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);
    if (!g_workerThread) return false;
    SetThreadPriority(g_workerThread, THREAD_PRIORITY_BELOW_NORMAL);

    InterlockedExchange(&g_active, 1);
    Log("[SavedVarsAsync] active (WriteFile + lazy GetFinalPathNameByHandle, queue=%u)",
       (unsigned)QUEUE_SIZE);
    return true;
}

void Flush() {
    if (!g_active) return;
    SetEvent(g_wakeupEvent);
    DWORD start = GetTickCount();
    while (g_queueHead != g_queueTail && (GetTickCount() - start) < 5000) {
        ResetEvent(g_drainEvent);
        SetEvent(g_wakeupEvent);
        WaitForSingleObject(g_drainEvent, 50);
    }
}

void Shutdown() {
    InterlockedExchange(&g_active, 0);
    Flush();
    InterlockedExchange(&g_stopRequested, 1);
    if (g_wakeupEvent) SetEvent(g_wakeupEvent);
    if (g_workerThread) {
        if (WaitForSingleObject(g_workerThread, 5000) == WAIT_TIMEOUT)
            TerminateThread(g_workerThread, 0);
        CloseHandle(g_workerThread);
        g_workerThread = nullptr;
    }
    if (g_wakeupEvent) { CloseHandle(g_wakeupEvent); g_wakeupEvent = nullptr; }
    if (g_drainEvent)  { CloseHandle(g_drainEvent);  g_drainEvent  = nullptr; }
    Log("[SavedVarsAsync] shutdown async=%lld sync=%lld bytes=%lld fails=%lld",
       (long long)g_writesAsync, (long long)g_writesSync,
       (long long)g_bytesAsync, (long long)g_writeFailures);
}

void GetStats(Stats* out) {
    if (!out) return;
    out->active        = g_active != 0;
    out->writesAsync   = (uint64_t)g_writesAsync;
    out->writesSync    = (uint64_t)g_writesSync;
    out->bytesAsync    = (uint64_t)g_bytesAsync;
    out->writeFailures = (uint64_t)g_writeFailures;
    {
        std::lock_guard<std::mutex> g(g_handleMutex);
        out->taggedHandles = (uint32_t)g_handleClass.size();
    }
    LONG h = g_queueHead, t = g_queueTail;
    out->queueDepth = (uint32_t)((h - t) & (LONG)QUEUE_MASK);
}

} // namespace SavedVarsAsync
