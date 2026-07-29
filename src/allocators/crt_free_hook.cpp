// ============================================================================
// Module: crt_free_hook.cpp
// Description: Removes a dead _msize call from WoW's CRT free wrapper.
// Safety & Threading: Concurrent safe. Calls WoW's own free, never this DLL's.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crt_free_hook.h"
#include "config.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

// WoW's CRT free wrapper, read out of the binary rather than assumed:
//
//   int __stdcall sub_76E5A0(void *Block, int a2, int a3, int a4) {
//       if (Block) { _msize(Block); free(Block); }
//       return 1;
//   }
//
// The _msize result is computed and thrown away on every call, and it is not
// cheap to compute: _msize (0x4112F8) ends in HeapSize(hHeap, 0, Block), and on
// the small-block-heap path it takes _lock(4) first. So every deallocation in
// the client pays a heap lookup, sometimes a lock, for a number nobody reads.
//
// Two independent tester profiles put RtlSizeHeap at 8.09% and 10.59% of the
// time the main thread spent executing, and this wrapper has over a hundred call
// sites across the binary. That is the cost being removed.
//
// The replacement is the same function minus the dead call: same condition, same
// free, same return value. It has to call WoW's free at 0x412FC7 rather than
// this DLL's, because the DLL links its own static CRT and returning a WoW-heap
// block through that would corrupt the heap.
static const uintptr_t WOW_FREE_WRAPPER = 0x0076E5A0;

typedef int  (__stdcall *crt_free_wrapper_t)(void* block, int, int, int);
typedef void (__cdecl   *wow_free_t)(void*);

static const wow_free_t   g_wow_free  = (wow_free_t)0x00412FC7;
static crt_free_wrapper_t g_orig      = nullptr;
static bool               g_installed = false;
static int                g_token     = -1;

// Deliberately a non-atomic 32-bit counter, and deliberately not 64-bit.
//
// An interlocked increment on every free would be a real cost on one of the
// hottest paths in the process - the same mistake the memset hook's counters
// made before they were removed. But a plain 64-bit increment is worse than
// unsynchronised: on 32-bit x86 it compiles to add/adc across two words, so a
// race does not merely lose a count, it can tear one and produce a number that
// was never true. An aligned 32-bit increment can only ever lose increments.
//
// So the count is a lower bound, and is reported as one. It wraps after about
// 4.3 billion deallocations; a session that busy is worth knowing about anyway.
static volatile LONG g_calls = 0;

int __stdcall Hooked_CrtFree(void* block, int a2, int a3, int a4) {
    ++g_calls;
    if (block) g_wow_free(block);
    return 1;
}

bool InstallCrtFreeHook() {
    if (!Config::g_settings.OptCrtFreeMsize) {
        Log("[CrtFree] DISABLED via configuration");
        return false;
    }

    void* target = (void*)WOW_FREE_WRAPPER;
    if (WineSafe_CreateHook(target, (void*)Hooked_CrtFree, (void**)&g_orig) != MH_OK) {
        Log("[CrtFree] ERROR: could not create hook at 0x%08X", (unsigned)WOW_FREE_WRAPPER);
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[CrtFree] ERROR: could not enable hook at 0x%08X", (unsigned)WOW_FREE_WRAPPER);
        return false;
    }

    g_token = CrashDumper::FeatureTokenForCounting("CrtFreeHook");
    g_installed = true;
    Log("[CrtFree] ACTIVE at 0x%08X - dropping the discarded _msize from every free",
        (unsigned)WOW_FREE_WRAPPER);
    return true;
}

void UninstallCrtFreeHook() {
    if (!g_installed) return;
    void* target = (void*)WOW_FREE_WRAPPER;
    MH_DisableHook(target);
    MH_RemoveHook(target);
    g_installed = false;
}

void ReportCrtFreeStats() {
    // Never measured and measured zero are different answers; say which.
    if (!g_installed) {
        Log("[CrtFree] not installed - no deallocations were measured");
        return;
    }
    Log("[CrtFree] at least %lu deallocations served, each one a HeapSize call "
        "not made (count is a lower bound - see the note on g_calls)",
        (unsigned long)g_calls);
}

void GetCrtFreeStats(uint64_t* hits, uint64_t* total) {
    if (hits)  *hits  = (uint64_t)(unsigned long)g_calls;
    if (total) *total = (uint64_t)(unsigned long)g_calls;
}
