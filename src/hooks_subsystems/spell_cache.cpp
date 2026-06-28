// ============================================================================
// Module: spell_cache.cpp
// Description: Supporting utility functions for `spell_cache.cpp`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================

#include "spell_cache.h"
#include "MinHook.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace SpellCache {

bool Init() {
    Log("[SpellCache] DISABLED - target function uses __usercall calling convention");
    Log("[SpellCache] Hooking custom calling conventions requires naked functions");
    return false;
}

void Shutdown() {
    // Nothing to do
}

void GetStats(Stats* stats) {
    if (!stats) return;
    stats->hits = 0;
    stats->misses = 0;
    stats->evictions = 0;
    stats->cacheSize = 0;
}

void Clear() {
    // Nothing to do
}

} // namespace SpellCache
