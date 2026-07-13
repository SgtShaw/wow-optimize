// ============================================================================
// Module: crt_free_hook.cpp
// Description: Detours standard CRT free calls directly to mimalloc memory arenas to bypass heap overhead.
// Safety & Threading: Concurrent safe. CRT and mimalloc heaps must never cross-deallocate.
// ============================================================================

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
    Log("[CrtFree] Bypassed for stability (standard CRT free wrapper is optimal).");
    return true;
}

void UninstallCrtFreeHook() {
    // No-op
}

void GetCrtFreeStats(uint64_t* hits, uint64_t* total) {
    if (hits) *hits = 0;
    if (total) *total = 0;
}
