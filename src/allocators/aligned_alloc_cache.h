#pragma once

// ============================================================================
// Module: aligned_alloc_cache.h
// Description: Supporting utility functions for `aligned_alloc_cache.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `aligned_alloc_cache.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: High-Performance Memory Allocations
 * @architecture: Overrides the standard CRT memory management callbacks using mimalloc redirects.
 * @thread_affinity: Worker Thread / Concurrent Execution Safe
 * @regression_hazard: Mismatched allocations between CRT heaps and mimalloc will cause instant heap corruption.
 */



bool InstallAlignedAllocCache();
void UninstallAlignedAllocCache();
