// ================================================================
// Predictive MPQ Prefetcher — Header
// WoW 3.3.5a build 12340
// ================================================================

#pragma once
#include <windows.h>

namespace MPQPrefetch {

struct Stats {
    long filesQueued;
    long filesCompleted;
    long cacheHits;
    long cacheMisses;
    long zoneTransitions;
    long queueDepth;
    double totalPrefetchTimeMs;
};

bool Init();
void Shutdown();
void ClearQueues();
void OnFrame(DWORD mainThreadId);
Stats GetStats();

} // namespace MPQPrefetch
