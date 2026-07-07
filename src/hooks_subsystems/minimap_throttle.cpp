#include <windows.h>
#include <cstdint>
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace MinimapThrottle {

static DWORD g_lastUpdateTick = 0;
static uint64_t g_ticksSkipped = 0;
static uint64_t g_ticksTotal = 0;

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

bool Init() {
    g_lastUpdateTick = GetTickCount();
    Log("[MinimapThrottle] Active - Minimap update throttling to 15Hz enabled");
    return true;
}

void Shutdown() {
    Log("[MinimapThrottle] Stats: Skipped %lld / %lld minimap redraw ticks (%.1f%% saving)",
        g_ticksSkipped, g_ticksTotal, g_ticksTotal ? 100.0 * g_ticksSkipped / g_ticksTotal : 0.0);
}

} // namespace MinimapThrottle
