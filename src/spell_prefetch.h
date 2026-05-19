#pragma once
// ================================================================
// Async Spell Data Prefetcher
// 
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
