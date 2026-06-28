#pragma once

// ============================================================================
// Module: datastore_fastpath.h
// Description: Supporting utility functions for `datastore_fastpath.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `datastore_fastpath.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */


#ifndef DATASTORE_FASTPATH_H
#define DATASTORE_FASTPATH_H

// CDataStore Fast Path Optimizations
// Hooks sub_47B3C0/sub_47B0A0/sub_47B340/sub_47AFE0/sub_47B100/sub_47B400
// TLS-cached buffer pointer to eliminate repeated base arithmetic.
// Total: ~4179 xrefs across network packet processing hot paths.

bool InitDataStoreFastPath();
void ShutdownDataStoreFastPath();
void DumpDataStoreStats();

#endif // DATASTORE_FASTPATH_H
