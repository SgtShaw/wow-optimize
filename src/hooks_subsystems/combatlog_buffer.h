#pragma once

// ============================================================================
// Module: combatlog_buffer.h
// Description: Supporting utility functions for `combatlog_buffer.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `combatlog_buffer.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */



#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>

namespace CombatLogBuffer {

struct Stats {
    int64_t totalEvents;
    int64_t droppedEvents;
    int64_t forcedFlushes;
    int64_t recycledEntries;
    int64_t allocatedEntries;
    int32_t currentPending;
    int32_t peakPending;
    int32_t ringBufferSize;
    int32_t ringBufferInUse;
};

bool Init();
void OnFrame(DWORD mainThreadId);
void Shutdown();
Stats GetStats();

} // namespace CombatLogBuffer
