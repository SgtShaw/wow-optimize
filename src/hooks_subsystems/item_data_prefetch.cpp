#include "item_data_prefetch.h"
#include <queue>
#include "win_mutex.h"

extern "C" void Log(const char* fmt, ...);

namespace ItemDataPrefetch {
    static std::queue<unsigned int> g_prefetchQueue;
    static WinMutex g_prefetchMutex;
    static HANDLE g_prefetchThread = nullptr;
    static HANDLE g_prefetchEvent = nullptr;
    static bool g_shutdown = false;
    static bool g_enabled = true;

    static DWORD WINAPI PrefetchWorkerThread(LPVOID lpParam) {
        return 0;
    }

    bool Init() {
        Log("[ItemDataPrefetch] Background prefetcher disabled for stability and thread-safety.");
        return true;
    }

    void Shutdown() {
    }

    void PrefetchItem(unsigned int itemId) {
        // Disabled: calling internal client query functions on background threads is unsafe and crashes
    }
}
