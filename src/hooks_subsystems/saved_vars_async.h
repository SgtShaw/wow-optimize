#pragma once

// ============================================================================
// Module: saved_vars_async.h
// Description: Supporting utility functions for `saved_vars_async.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `saved_vars_async.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */


// SavedVariables Async Writer
// Offloads SV serialization to background thread so logout/reload
// doesn't block the main thread on large addon data writes.
bool InstallSavedVarsAsync();
void ShutdownSavedVarsAsync();
