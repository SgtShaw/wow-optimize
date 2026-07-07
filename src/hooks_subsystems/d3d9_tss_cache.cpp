#include <windows.h>
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace D3D9TssCache {

bool Init() {
    Log("[D3D9TssCache] Active - Redundant Texture Stage State cache mapped via D3D9StateCache");
    return true;
}

void Shutdown() {
    // Linked shutdown
}

} // namespace D3D9TssCache
