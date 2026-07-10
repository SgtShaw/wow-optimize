#include <windows.h>
#include "minimap_throttle.h"

extern "C" void Log(const char* fmt, ...);

namespace MinimapThrottle {

bool Init() {
    Log("[MinimapThrottle] Disabled (preventing rendering artifacts)");
    return true;
}

void Shutdown() {
    // No-op
}

bool ShouldUpdate() {
    return true;
}

} // namespace MinimapThrottle
