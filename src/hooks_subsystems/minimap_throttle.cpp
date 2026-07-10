#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "minimap_throttle.h"

extern "C" void Log(const char* fmt, ...);

static inline bool IsTeardownState() {
    uintptr_t gL = *(uintptr_t*)0x00D3F78C;
    return (gL < 0x10000 || gL > 0xFFE00000);
}

namespace MinimapThrottle {

static DWORD g_lastUpdateTick = 0;
static uint64_t g_ticksSkipped = 0;
static uint64_t g_ticksTotal = 0;

static bool g_shouldUpdateThisFrame = true;
static bool g_inMinimapUpdate = false;

typedef void (__thiscall* MinimapTick_t)(void* self);
static MinimapTick_t orig_MinimapTick = nullptr;

typedef double (__cdecl* MinimapMovement_t)(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8);
static MinimapMovement_t orig_MinimapMovement = nullptr;

// Returns true if the frame draw should be allowed, false to skip redundant minimap ticks
bool ShouldUpdate() {
    g_ticksTotal++;
    DWORD now = GetTickCount();
    if (now - g_lastUpdateTick < 66) { // limit updates to ~15Hz
        g_ticksSkipped++;
        return false;
    }
    g_lastUpdateTick = now;
    return true;
}

static void __fastcall Hooked_MinimapTick(void* self, void* unused) {
    if (!IsTeardownState()) {
        g_shouldUpdateThisFrame = ShouldUpdate();
    } else {
        g_shouldUpdateThisFrame = true;
    }

    g_inMinimapUpdate = true;
    orig_MinimapTick(self);
    g_inMinimapUpdate = false;
}

static double __cdecl Hooked_MinimapMovement(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
    if (g_inMinimapUpdate && !g_shouldUpdateThisFrame) {
        return 0.0; // Force no-movement path to bypass recalculation on throttled frames!
    }
    return orig_MinimapMovement(a1, a2, a3, a4, a5, a6, a7, a8);
}

bool Init() {
    g_lastUpdateTick = GetTickCount();
    g_shouldUpdateThisFrame = true;
    g_inMinimapUpdate = false;

    if (WineSafe_CreateHook((void*)0x00581E80, (void*)Hooked_MinimapTick, (void**)&orig_MinimapTick) == MH_OK) {
        if (MH_EnableHook((void*)0x00581E80) != MH_OK) {
            Log("[MinimapThrottle] Failed to enable hook on CGMinimapFrame::OnUpdate");
            return false;
        }
    } else {
        Log("[MinimapThrottle] Failed to create hook on CGMinimapFrame::OnUpdate");
        return false;
    }

    if (WineSafe_CreateHook((void*)0x007F5BA0, (void*)Hooked_MinimapMovement, (void**)&orig_MinimapMovement) == MH_OK) {
        if (MH_EnableHook((void*)0x007F5BA0) != MH_OK) {
            Log("[MinimapThrottle] Failed to enable hook on MinimapMovement");
            return false;
        }
    } else {
        Log("[MinimapThrottle] Failed to create hook on MinimapMovement");
        return false;
    }

    Log("[MinimapThrottle] Active - Minimap update throttling to 15Hz enabled (drawing unthrottled)");
    return true;
}

void Shutdown() {
    MH_DisableHook((void*)0x00581E80);
    MH_DisableHook((void*)0x007F5BA0);
    Log("[MinimapThrottle] Stats: Skipped %lld / %lld minimap redraw ticks (%.1f%% saving)",
        g_ticksSkipped, g_ticksTotal, g_ticksTotal ? 100.0 * g_ticksSkipped / g_ticksTotal : 0.0);
}

} // namespace MinimapThrottle
