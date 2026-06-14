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

typedef int  (__stdcall *crt_free_t)(void* block, int, int, int);
static crt_free_t g_orig = nullptr;

// WoW's own CRT free. The wrapper at 0x0076E5A0 does `if (b){ _msize(b); free(b); }`
// -- the _msize result is computed and discarded on every call, a wasted heap
// lock + metadata read on the second-hottest function in the binary. Call free
// directly to drop it. Must use WoW's allocator (this DLL links a separate
// static CRT, so freeing a WoW-heap block with our own free would corrupt).
typedef void (__cdecl *wow_free_t)(void*);
static const wow_free_t g_wow_free = (wow_free_t)0x00412FC7;

int __stdcall Hooked_CrtFree(void* block, int, int, int) {
    if (block) g_wow_free(block);
    return 1;
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

    Log("[CrtFree] Installed: free wrapper (2901 callers, _msize elided)");
    return true;
}

void UninstallCrtFreeHook() {
    MH_DisableHook((void*)0x0076E5A0);
    MH_RemoveHook((void*)0x0076E5A0);
}

void GetCrtFreeStats(uint64_t* hits, uint64_t* total) {
    if (hits) *hits = 0;
    if (total) *total = 0;
}
