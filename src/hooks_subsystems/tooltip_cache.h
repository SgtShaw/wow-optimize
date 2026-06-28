#pragma once

// ============================================================================
// Module: tooltip_cache.h
// Description: Supporting utility functions for `tooltip_cache.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `tooltip_cache.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */



#include <cstdint>

namespace TooltipCache {

struct Stats {
    int64_t hits;
    int64_t misses;
    int64_t evictions;
    double hitRate;
};

bool Install();
void Shutdown();
void Clear();
Stats GetStats();

// Cache API for external integration
const char* Get(uint32_t key, int* outLen);
void Put(uint32_t key, const char* text, int len);
uint32_t Hash(uint32_t itemID, uint32_t flags, uint32_t extra);

} // namespace TooltipCache