#include "frame_script_mem_opt.h"
#include "win_mutex.h"
#include <vector>

namespace FrameScriptMemOpt {
    static bool g_enabled = true;
    static std::vector<void*> g_scriptBlocksPool;
    static WinMutex g_poolMutex;
    static constexpr size_t POOL_LIMIT = 64;
    static constexpr size_t FIXED_BLOCK_SIZE = 512; // Common script closure block size

    bool Init() {
        return true;
    }

    void Shutdown() {
        WinLockGuard lock(g_poolMutex);
        for (void* ptr : g_scriptBlocksPool) {
            HeapFree(GetProcessHeap(), 0, ptr);
        }
        g_scriptBlocksPool.clear();
    }

    void* AllocateScriptBlock(size_t size) {
        if (!g_enabled || size != FIXED_BLOCK_SIZE) {
            return HeapAlloc(GetProcessHeap(), 0, size);
        }

        WinLockGuard lock(g_poolMutex);
        if (!g_scriptBlocksPool.empty()) {
            void* block = g_scriptBlocksPool.back();
            g_scriptBlocksPool.pop_back();
            return block;
        }

        return HeapAlloc(GetProcessHeap(), 0, FIXED_BLOCK_SIZE);
    }

    void FreeScriptBlock(void* ptr) {
        if (!ptr) return;
        if (!g_enabled) {
            HeapFree(GetProcessHeap(), 0, ptr);
            return;
        }

        WinLockGuard lock(g_poolMutex);
        if (g_scriptBlocksPool.size() < POOL_LIMIT) {
            g_scriptBlocksPool.push_back(ptr);
        } else {
            HeapFree(GetProcessHeap(), 0, ptr);
        }
    }
}
