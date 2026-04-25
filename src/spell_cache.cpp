// ================================================================
// Spell Data Caching Implementation
// ================================================================
// 
// NOTE: This optimization is DISABLED because the target function at 0x80E1B0
// uses a custom calling convention (__usercall) that is extremely difficult
// to hook correctly:
//   - Parameters passed in registers: a1@<eax>, a2 on stack
//   - Return value in eax
//   - Custom register preservation rules
//
// Attempting to hook this with standard calling conventions causes crashes.
// A proper implementation would require:
//   1. Naked function wrapper with inline assembly
//   2. Manual register save/restore
//   3. Custom trampoline generation
//
// This is beyond the scope of a simple optimization and the risk/reward
// ratio is not favorable. The tooltip cache provides better ROI.
// ================================================================

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
