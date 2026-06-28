#pragma once

// ============================================================================
// Module: ui_cache.h
// Description: Supporting utility functions for `ui_cache.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `ui_cache.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */


#ifndef UI_CACHE_H
#define UI_CACHE_H

#include <windows.h>

namespace UICache {

bool Init();
void Shutdown();
void ClearCache();

struct Stats {
    long skipped;
    long passed;
    bool active;
};

Stats GetStats();

} // namespace UICache

#endif