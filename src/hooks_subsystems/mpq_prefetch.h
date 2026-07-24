#pragma once

// ============================================================================
// Module: mpq_prefetch.h
// ============================================================================

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
void QueuePrefetch(const char* filename);
Stats GetStats();

// Feature 21 (0x0083AF90): Asynchronous MPQ Sector Prefetcher
void OptimizeSub83AF90_AsyncMPQ(const char* mpqFileName);

} // namespace MPQPrefetch
