#pragma once

// ============================================================================
// Module: texcache_tuning.h
// Description: Supporting utility functions for `texcache_tuning.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `texcache_tuning.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: High-Performance Memory Allocations
 * @architecture: Overrides the standard CRT memory management callbacks using mimalloc redirects.
 * @thread_affinity: Worker Thread / Concurrent Execution Safe
 * @regression_hazard: Mismatched allocations between CRT heaps and mimalloc will cause instant heap corruption.
 */



// Raises WoW's resident-texture cache budget so an area's working set of textures
// stays resident instead of being evicted and reloaded constantly. See the .cpp
// for the full binary rationale. Tick() re-asserts after device resets.
void InitTexCacheTuning();
void TexCacheTuning_Tick();

// Dynamic budget control for the memory-pressure governor.
// SetBudget(N) writes directly to 0xB49C9C (no bound check — caller is trusted).
// GetConfiguredTarget() returns the budget selected at init (128/96 MB).
void TexCache_SetBudget(int bytes);
int  TexCache_GetConfiguredTarget();
