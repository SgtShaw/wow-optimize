/*
 * Free wrapper hook
 * Target: 0x0076E5A0 (2901 callers)
 * int __stdcall(void* block, int, int, int) -> free wrapper
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <atomic>
#include "MinHook.h"
#include "crt_free_hook.h"

extern "C" void Log(const char* fmt, ...);

static std::atomic<uint64_t> g_calls{0};
static std::atomic<uint64_t> g_null_skips{0};
static std::atomic<uint64_t> g_frees{0};

typedef int (__stdcall *crt_free_t)(void* block, int, int, int);
static crt_free_t g_orig = nullptr;

int __stdcall Hooked_CrtFree(void* block, int a2, int a3, int a4) {
    g_calls.fetch_add(1, std::memory_order_relaxed);

    if (!block) {
        g_null_skips.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }

    g_frees.fetch_add(1, std::memory_order_relaxed);
    return g_orig(block, a2, a3, a4);
}

bool InstallCrtFreeHook() {
    void* target = (void*)0x0076E5A0;

    if (MH_CreateHook(target, (void*)Hooked_CrtFree, (void**)&g_orig) != MH_OK) {
        Log("[CrtFree] Failed to create hook at 0x0076E5A0");
        return false;
    }

    if (MH_EnableHook(target) != MH_OK) {
        Log("[CrtFree] Failed to enable hook");
        MH_RemoveHook(target);
        return false;
    }

    Log("[CrtFree] Installed: free wrapper (2901 callers)");
    return true;
}

void UninstallCrtFreeHook() {
    MH_DisableHook((void*)0x0076E5A0);
    MH_RemoveHook((void*)0x0076E5A0);

    uint64_t calls = g_calls.load();
    uint64_t nulls = g_null_skips.load();
    uint64_t frees = g_frees.load();

    if (calls > 0) {
        Log("[CrtFree] Stats: %llu calls, %llu null skips, %llu actual frees",
            calls, nulls, frees);
    }
}

void GetCrtFreeStats(uint64_t* hits, uint64_t* total) {
    if (hits) *hits = g_frees.load(std::memory_order_relaxed);
    if (total) *total = g_calls.load(std::memory_order_relaxed);
}
