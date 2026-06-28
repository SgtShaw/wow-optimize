#pragma once

// ============================================================================
// Module: strcat_fast.h
// Description: SSE2 vectorized replacement for legacy CRT function `strcat_fast.h`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================


/**
 * @domain: Statically Linked CRT Replacement
 * @architecture: Replaces legacy x86 compiler intrinsic routines in `strcat_fast.h` with vectorized SSE2.
 * @thread_affinity: Concurrent Execution Safe
 * @regression_hazard: Ensure page-boundary checks are active to prevent unaligned SSE reads from triggering page faults.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */


#ifndef STRCAT_FAST_H
#define STRCAT_FAST_H

#include <cstdint>

bool InstallStrcatFast();
void StrcpyFast_GetStats(uint64_t* fast, uint64_t* fallback);

#endif // STRCAT_FAST_H
