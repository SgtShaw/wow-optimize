// Stream reader/writer cache - disabled. __thiscall requires naked asm which
// cannot safely use __declspec(thread) (needs manual FS:[0x2C] indirection).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
extern "C" void Log(const char* fmt, ...);

bool InstallStreamCache() {
    Log("[StreamCache] Disabled: __thiscall + naked asm TLS issue");
    return true;
}
void UninstallStreamCache() {}
void GetStreamCacheStats(uint64_t* rHits, uint64_t* rTotal, uint64_t* wHits, uint64_t* wTotal) {
    if (rHits) *rHits = 0;
    if (rTotal) *rTotal = 0;
    if (wHits) *wHits = 0;
    if (wTotal) *wTotal = 0;
}
