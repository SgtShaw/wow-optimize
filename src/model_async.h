// ================================================================
// Async Model/M2 Loader — Header
// WoW 3.3.5a build 12340
// ================================================================

#pragma once

#include <windows.h>

namespace ModelAsync {

struct Stats {
    long requestsQueued;
    long requestsCompleted;
    long requestsDropped;
    long cacheHits;
    long cacheMisses;
    long queueDepth;
    double totalLoadTimeMs;
};

bool Init();
void Shutdown();
void OnFrame(DWORD mainThreadId);
Stats GetStats();

} // namespace ModelAsync
