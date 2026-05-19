// ================================================================
// Predictive MPQ Prefetcher — Header
// ================================================================

#pragma once

#include <windows.h>

namespace MPQPrefetch {

struct Stats {
    long filesQueued;
    long filesCompleted;
    long filesDropped;
    long cacheHits;
    long cacheMisses;
    long queueDepth;
    long zoneTransitions;
    double totalPrefetchTimeMs;
};

bool Init();
void Shutdown();
void OnFrame(DWORD mainThreadId);
Stats GetStats();

} // namespace MPQPrefetch
