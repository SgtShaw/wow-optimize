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

typedef void (__thiscall* MinimapTick_t)(void* self);
static MinimapTick_t orig_MinimapTick = nullptr;

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
        if (!ShouldUpdate()) {
            return; // skip this minimap tick frame!
        }
    }
    orig_MinimapTick(self);
}

bool Init() {
    g_lastUpdateTick = GetTickCount();

    if (WineSafe_CreateHook((void*)0x00581E80, (void*)Hooked_MinimapTick, (void**)&orig_MinimapTick) == MH_OK) {
        if (MH_EnableHook((void*)0x00581E80) == MH_OK) {
            Log("[MinimapThrottle] Hooked CGMinimapFrame::OnUpdate at 0x00581E80 successfully");
        } else {
            Log("[MinimapThrottle] Failed to enable hook on CGMinimapFrame::OnUpdate");
            return false;
        }
    } else {
        Log("[MinimapThrottle] Failed to create hook on CGMinimapFrame::OnUpdate");
        return false;
    }

    Log("[MinimapThrottle] Active - Minimap update throttling to 15Hz enabled");
    return true;
}

void Shutdown() {
    MH_DisableHook((void*)0x00581E80);
    Log("[MinimapThrottle] Stats: Skipped %lld / %lld minimap redraw ticks (%.1f%% saving)",
        g_ticksSkipped, g_ticksTotal, g_ticksTotal ? 100.0 * g_ticksSkipped / g_ticksTotal : 0.0);
}

} // namespace MinimapThrottle
