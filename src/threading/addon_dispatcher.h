#pragma once

// ============================================================================
// Module: addon_dispatcher.h
// Description: Supporting utility functions for `addon_dispatcher.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `addon_dispatcher.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Asynchronous Worker Thread Pools
 * @architecture: Manages lock-free task queuing and dispatches work asynchronously to worker threads.
 * @thread_affinity: Lock-Free Async Worker Pool
 * @regression_hazard: Thread synchronization issues or data race conditions will cause thread deadlocks or memory corruption.
 */


// ================================================================
// Multithreaded Addon Update Dispatcher
// 
// ================================================================

#ifndef ADDON_DISPATCHER_H
#define ADDON_DISPATCHER_H

#include <windows.h>
#include <cstdint>

namespace AddonDispatcher {

// Statistics structure
struct Stats {
    volatile LONG callbacksQueued;      // Total callbacks queued
    volatile LONG callbacksProcessed;   // Total callbacks processed
    volatile LONG callbacksDropped;     // Callbacks dropped (queue full)
    volatile LONG batchesProcessed;     // Total batches processed
    volatile LONG queueDepth;           // Current queue depth
    double totalProcessTimeMs;          // Total processing time in milliseconds
    double avgBatchSize;                // Average batch size
};

// Initialize the multithreaded addon dispatcher
bool Init();

// Shutdown and cleanup
void Shutdown();

// Called from main thread on each frame (for batch processing)
void OnFrame(DWORD mainThreadId);

// Get current statistics
Stats GetStats();

} // namespace AddonDispatcher

#endif // ADDON_DISPATCHER_H
