#pragma once

// ============================================================================
// Module: stream_cache.h
// Description: SSE2 vectorized replacement for legacy CRT function `stream_cache.h`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================


/**
 * @domain: Statically Linked CRT Replacement
 * @architecture: Replaces legacy x86 compiler intrinsic routines in `stream_cache.h` with vectorized SSE2.
 * @thread_affinity: Concurrent Execution Safe
 * @regression_hazard: Ensure page-boundary checks are active to prevent unaligned SSE reads from triggering page faults.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */



#include <cstdint>

bool InstallStreamCache();
void UninstallStreamCache();
void GetStreamCacheStats(uint64_t* rHits, uint64_t* rTotal, uint64_t* wHits, uint64_t* wTotal);
