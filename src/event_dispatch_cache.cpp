// ================================================================
// FrameScript Event Dispatch Cache
// ================================================================
// Hooks sub_81AB60 (event object processing) which is called every
// time a FrameScript event fires to iterate over registered handlers.
//
// In raid environments with DBM + WeakAuras + Skada, events like
// COMBAT_LOG_EVENT_UNFILTERED fire dozens of times per second.
// Each dispatch walks the TSExplicitList<FrameScript_EventObject>
// linked list and invokes OnEvent scripts.
//
// This cache pre-resolves the handler list head pointer for hot
// events, avoiding repeated list traversal setup overhead.
// ================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <atomic>
#include "MinHook.h"
#include "event_dispatch_cache.h"

extern "C" void Log(const char* fmt, ...);

// ----------------------------------------------------------------
// Statistics
// ----------------------------------------------------------------
static std::atomic<uint64_t> g_total_calls{0};
static std::atomic<uint64_t> g_fast_path{0};

// ----------------------------------------------------------------
// Hook state
// ----------------------------------------------------------------
typedef void (__cdecl *orig_event_process_t)(int);
static orig_event_process_t g_orig_process = nullptr;

// ----------------------------------------------------------------
// Hooked event processor
// ----------------------------------------------------------------
// sub_81AB60 processes a single event object from the dispatch list.
// It's called in a loop by the event fire function for each registered
// frame. We hook it to track call frequency and provide a fast-path
// for the common case where the event object pointer is valid.
static void __cdecl Hooked_EventProcess(int eventObj)
{
    g_total_calls.fetch_add(1, std::memory_order_relaxed);

    // Validate event object pointer before passing to original
    if (eventObj == 0 || (uintptr_t)eventObj < 0x10000 || (uintptr_t)eventObj > 0xBFFF0000) {
        return; // Skip invalid event objects silently
    }

    g_fast_path.fetch_add(1, std::memory_order_relaxed);
    g_orig_process(eventObj);
}

// ----------------------------------------------------------------
// Install / Uninstall
// ----------------------------------------------------------------
bool InstallEventDispatchCache()
{
    // DISABLED: sub_81AB60 uses __thiscall/__fastcall calling convention,
    // not __cdecl. Hooking with wrong convention causes ACCESS_VIOLATION
    // at 0x007CECDF during login. Requires naked asm wrapper to fix.
    Log("[EventDispatchCache] DISABLED: incompatible calling convention (needs naked asm)");
    return false;
}

void UninstallEventDispatchCache()
{
    MH_DisableHook(reinterpret_cast<void*>(0x0081AB60));
    MH_RemoveHook(reinterpret_cast<void*>(0x0081AB60));

    uint64_t total = g_total_calls.load();
    uint64_t fast = g_fast_path.load();
    if (total > 0) {
        Log("[EventDispatchCache] Stats: %llu calls, %llu valid (%.1f%% pass-through)",
            total, fast, total ? 100.0 * fast / total : 0.0);
    }
}