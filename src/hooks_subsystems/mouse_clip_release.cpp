#include "mouse_clip_release.h"

namespace MouseClipRelease {

static bool g_hasFocus = true;

bool Init() {
    return true;
}

void Shutdown() {
    ClipCursor(nullptr);
}

void OnFocusChange(bool hasFocus) {
    g_hasFocus = hasFocus;
    if (!g_hasFocus) {
        // Release screen boundary clip locks on mouse cursor coordinates
        ClipCursor(nullptr);
    }
}

} // namespace MouseClipRelease
