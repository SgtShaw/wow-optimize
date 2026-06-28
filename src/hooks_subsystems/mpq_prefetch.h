#pragma once

// ============================================================================
// Module: mpq_prefetch.h
// Description: Supporting utility functions for `mpq_prefetch.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `mpq_prefetch.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */



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
