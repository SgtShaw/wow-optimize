// ============================================================================
// Module: io_cache.cpp
// Description: Supporting utility functions for `io_cache.cpp`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
extern "C" void Log(const char* fmt, ...);

bool InstallIOCache() {
    Log("[IOCache] Disabled: dispatcher has writes/critical sections");
    return true;
}
void UninstallIOCache() {}
