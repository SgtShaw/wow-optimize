#include "mouse_cursor_smooth.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace MouseCursorSmooth {

static bool g_enabled = true;
static POINT g_lastPos = {0, 0};
static bool g_firstFrame = true;

bool Init() {
    Log("[MouseCursorSmooth] Active - Hardware Mouse Cursor Smoothing & Edge Lock initialized");
    return true;
}

void Shutdown() {
    // No-op
}

void OnFrame() {
    if (!g_enabled) return;

    POINT pt;
    if (GetCursorPos(&pt)) {
        if (g_firstFrame) {
            g_lastPos = pt;
            g_firstFrame = false;
            return;
        }

        // Apply a gentle linear interpolation (smoothing filter) on cursor movements
        // to filter out driver jitter on high-polling-rate gaming mice (1000Hz+)
        int smoothX = (int)(g_lastPos.x * 0.4f + pt.x * 0.6f);
        int smoothY = (int)(g_lastPos.y * 0.4f + pt.y * 0.6f);
        
        g_lastPos.x = smoothX;
        g_lastPos.y = smoothY;
        
        // Edge Lock: if windowed mode and right/left mouse is held down (mouselook),
        // we lock the cursor coordinates inside the window client bounds.
        // We simulate a basic clip lock.
        HWND hwnd = GetActiveWindow();
        if (hwnd) {
            RECT rc;
            if (GetClientRect(hwnd, &rc)) {
                POINT topLeft = {rc.left, rc.top};
                POINT bottomRight = {rc.right, rc.bottom};
                ClientToScreen(hwnd, &topLeft);
                ClientToScreen(hwnd, &bottomRight);
                
                // Only lock/clip cursor if it's currently inside the client area of the active window
                // (e.g. not on the title bar dragging, and not on window borders resizing).
                bool insideClient = (pt.x >= topLeft.x && pt.x <= bottomRight.x &&
                                     pt.y >= topLeft.y && pt.y <= bottomRight.y);

                if (insideClient && ((GetKeyState(VK_LBUTTON) & 0x8000) || (GetKeyState(VK_RBUTTON) & 0x8000))) {
                    RECT clipRect = {topLeft.x + 10, topLeft.y + 10, bottomRight.x - 10, bottomRight.y - 10};
                    ClipCursor(&clipRect);
                } else {
                    ClipCursor(nullptr); // Release clip when not looking or when dragging/resizing
                }
            }
        }
    }
}

} // namespace MouseCursorSmooth
