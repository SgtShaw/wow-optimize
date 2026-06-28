#pragma once

// ============================================================================
// Module: crt_free_hook.h
// Description: SSE2 vectorized replacement for legacy CRT function `crt_free_hook.h`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================


/**
 * @domain: Statically Linked CRT Replacement
 * @architecture: Replaces legacy x86 compiler intrinsic routines in `crt_free_hook.h` with vectorized SSE2.
 * @thread_affinity: Concurrent Execution Safe
 * @regression_hazard: Ensure page-boundary checks are active to prevent unaligned SSE reads from triggering page faults.
 */



/**
 * @domain: High-Performance Memory Allocations
 * @architecture: Overrides the standard CRT memory management callbacks using mimalloc redirects.
 * @thread_affinity: Worker Thread / Concurrent Execution Safe
 * @regression_hazard: Mismatched allocations between CRT heaps and mimalloc will cause instant heap corruption.
 */


#include <cstdint>

bool InstallCrtFreeHook();
void UninstallCrtFreeHook();
void GetCrtFreeStats(uint64_t* hits, uint64_t* total);
