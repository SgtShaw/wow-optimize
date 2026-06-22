// ================================================================
// FrameScript Event Dispatch Cache
// ================================================================
// Hooks sub_81AB60 (TSExplicitList management — resize/grow/shrink
// the FrameScript event-object list). This is called when the engine
// registers or unregisters frames for events, NOT on every event
// fire — so it runs at registration/unregistration frequency.
//
// Under ElvUI/WeakAuras, event registrations happen during UI init
// and zone transitions, not per-frame. Hooking this provides stats
// on list-management churn rather than a per-event fast path.
//
// The real hot path for events is sub_81AC90 (FrameScript_SignalEvent,
// the event-fire dispatcher), which is already hooked for coalescing
// (event_coalescer.cpp). This hook is complementary — it tracks how
// often the registration list is resized.
//
// Calling convention (IDA-verified):
//   unsigned int __thiscall(unsigned int* this, unsigned int newSize)
//   — this  in ECX
//   — newSize on the stack at [ebp+8]
//
// MinHook does not support __thiscall directly. The standard MSVC
// workaround uses __fastcall: first arg in ECX (matches this), second
// arg in EDX (dummy — the original never reads EDX), third+ on the
// stack (matches the __thiscall stack args layout).
// ================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <cstring>
#include "MinHook.h"
#include "version.h"
#include "event_dispatch_cache.h"

extern "C" void Log(const char* fmt, ...);

// ----------------------------------------------------------------
// Statistics
// ----------------------------------------------------------------
static volatile LONG64 g_total_calls  = 0;   // total hook invocations
static volatile LONG64 g_grow_calls   = 0;   // list grew (newSize > current)
static volatile LONG64 g_shrink_calls = 0;   // list shrunk (newSize < current)

// ----------------------------------------------------------------
// Hook state
// ----------------------------------------------------------------
// __fastcall(this=ECX, dummy=EDX, newSize on stack) matches
// __thiscall(this=ECX, newSize on stack) — the EDX dummy is unused
// by the original and is simply ignored.
typedef unsigned int (__fastcall *orig_fn_t)(unsigned int* self, int edx_dummy, unsigned int newSize);
static orig_fn_t g_orig = nullptr;

// ----------------------------------------------------------------
// Hooked event-list manager
// ----------------------------------------------------------------
static unsigned int __fastcall Hooked_EventProcess(
    unsigned int* self, int edx_dummy, unsigned int newSize)
{
    // edx_dummy is unused by the original — it only exists to make
    // the __fastcall calling convention align with __thiscall.

    g_total_calls++;

    // Pointer-validation: protect against a freed/corrupt self pointer.
    // sub_81AB60 reads self[0] (capacity), self[1] (size), self[2]
    // (data pointer), and self[3] (growth increment) — all at offsets
    // 0/4/8/12. Validate that the first 16 bytes are readable.
    if (!self || (uintptr_t)self < 0x10000 || (uintptr_t)self > 0xBFFF0000) {
        return 0;  // corrupt pointer — skip safely, return 0
    }

    // Track list growth vs shrink for diagnostics
    unsigned int current = self[1]; // current size at offset +4
    if (newSize > current)
        g_grow_calls++;
    else if (newSize < current)
        g_shrink_calls++;

    return g_orig(self, 0, newSize);
}

// ----------------------------------------------------------------
// Install / Uninstall
// ----------------------------------------------------------------
bool InstallEventDispatchCache()
{
    // Verify target address is still valid (prologue: push ebp; mov ebp, esp)
    unsigned char* p = (unsigned char*)0x0081AB60;
    if (p[0] != 0x55 || p[1] != 0x8B) {
        Log("[EventDispatchCache] BAD PROLOGUE at 0x0081AB60 (expected 55 8B, got %02X %02X) — offset changed?",
            p[0], p[1]);
        return false;
    }

    MH_STATUS st = WineSafe_CreateHook(
        (void*)0x0081AB60,
        (void*)Hooked_EventProcess,
        (void**)&g_orig);
    if (st != MH_OK) {
        Log("[EventDispatchCache] WineSafe_CreateHook FAILED (status %d)", (int)st);
        return false;
    }

    st = WO_EnableHook((void*)0x0081AB60);
    if (st != MH_OK) {
        Log("[EventDispatchCache] WO_EnableHook FAILED (status %d)", (int)st);
        MH_RemoveHook((void*)0x0081AB60);
        return false;
    }

    Log("[EventDispatchCache] ACTIVE (sub_81AB60 @ 0x0081AB60 — __fastcall thunk, stats-only pass-through)");
    return true;
}

void UninstallEventDispatchCache()
{
    MH_DisableHook((void*)0x0081AB60);
    MH_RemoveHook((void*)0x0081AB60);

    LONG64 total  = g_total_calls;
    LONG64 grow   = g_grow_calls;
    LONG64 shrink = g_shrink_calls;
    if (total > 0) {
        Log("[EventDispatchCache] Stats: %lld total, %lld grow, %lld shrink (%.0f%% resize)",
            total, grow, shrink,
            total > 0 ? 100.0 * (grow + shrink) / total : 0.0);
    }
}