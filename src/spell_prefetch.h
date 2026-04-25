#pragma once
// ================================================================
// Async Spell Data Prefetcher for wow_optimize.dll — build 12340
// 
// WHAT: Prefetches spell data before cast completes to eliminate
//       spell cast lag and microstutters.
// WHY:  WoW loads spell data synchronously during cast, causing
//       30-40ms stutters on every spell cast.
// HOW:  1. Hook spell cast start (sub_80CCE0 at 0x0080CCE0)
//       2. Queue spell data prefetch to worker thread
//       3. Load spell data asynchronously (sub_4CFD20 at 0x004CFD20)
//       4. Cache loaded data in memory
//       5. Return cached data when cast completes
// ADDRESSES:
//   - sub_80CCE0: 0x0080CCE0 (spell cast function)
//   - sub_4CFD20: 0x004CFD20 (spell data loader)
//   - Spell data structure size: 0x2A8 (680 bytes)
// STATUS: Colossal-scale optimization
// ================================================================

#ifndef SPELL_PREFETCH_H
#define SPELL_PREFETCH_H

#include <windows.h>
#include <cstdint>

namespace SpellPrefetch {

// Statistics structure
struct Stats {
    volatile LONG requestsQueued;      // Total prefetch requests queued
    volatile LONG requestsCompleted;   // Total prefetches completed
    volatile LONG requestsDropped;     // Requests dropped (queue full)
    volatile LONG cacheHits;           // Cache hits (already prefetched)
    volatile LONG cacheMisses;         // Cache misses (need to prefetch)
    volatile LONG queueDepth;          // Current queue depth
    double totalPrefetchTimeMs;        // Total prefetch time in milliseconds
};

// Initialize the async spell data prefetcher
bool Init();

// Shutdown and cleanup
void Shutdown();

// Called from main thread on each frame (for stats updates)
void OnFrame(DWORD mainThreadId);

// Get current statistics
Stats GetStats();

} // namespace SpellPrefetch

#endif // SPELL_PREFETCH_H
