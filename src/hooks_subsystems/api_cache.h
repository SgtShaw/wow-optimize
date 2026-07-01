#pragma once

// ============================================================================
// Module: api_cache.h
// Description: Supporting utility functions for `api_cache.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `api_cache.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */


#ifndef API_CACHE_H
#define API_CACHE_H

#include <windows.h>

struct lua_State;

namespace ApiCache {

bool Init();
void Shutdown();
void OnNewFrame();
void ClearCache();

struct Stats {
    long itemHits;
    long itemMisses;
    long spellHits;
    long spellMisses;
    bool active;
};

Stats GetStats();

} // namespace ApiCache

#endif