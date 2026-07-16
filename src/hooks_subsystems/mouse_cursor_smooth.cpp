#include "mouse_cursor_smooth.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace MouseCursorSmooth {

static bool g_enabled = true;
static POINT g_lastPos = {0, 0};
static bool g_firstFrame = true;

bool Init() {
    Log("[MouseCursorSmooth] Bypassed - Native mouse input preserved to prevent camera zoom/spin issues");
    return true;
}

void Shutdown() {
    // No-op
}

void OnFrame() {
    // Bypassed to prevent camera zoom/spin bugs caused by ClipCursor fighting with WoW's native centering.
}

} // namespace MouseCursorSmooth
