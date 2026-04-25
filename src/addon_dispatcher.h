#pragma once
// ================================================================
// Multithreaded Addon Update Dispatcher for wow_optimize.dll — build 12340
// 
// WHAT: Batches and parallelizes addon OnUpdate callbacks across
//       worker threads to reduce main thread CPU overhead.
// WHY:  Addons like DBM, Skada, WeakAuras run expensive OnUpdate
//       callbacks every frame, consuming 40-50% of main thread CPU.
// HOW:  1. Hook addon OnUpdate registration
//       2. Collect OnUpdate callbacks during frame
//       3. Dispatch callbacks to worker thread pool
//       4. Sync results back to main thread
//       5. Execute Lua callbacks in parallel
// STATUS: Colossal-scale optimization
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
