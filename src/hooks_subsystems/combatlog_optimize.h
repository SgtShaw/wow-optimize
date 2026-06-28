#pragma once

// ============================================================================
// Module: combatlog_optimize.h
// Description: Supporting utility functions for `combatlog_optimize.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `combatlog_optimize.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */


// Combat log optimizer.
// 1. Retention time increase (300 -> 1800 sec)
// 2. Periodic CombatLogClearEntries from C level

#ifndef COMBATLOG_OPTIMIZE_H
#define COMBATLOG_OPTIMIZE_H

#include <windows.h>
#include <cstdint>

namespace CombatLogOpt {

bool Init();
void OnFrame(DWORD mainThreadId);
void Shutdown();
void SetCombat(bool active);

struct PoolStats {
    int  poolSize;
    int  poolUsed;
    bool poolActive;
};

PoolStats GetPoolStats();

} // namespace CombatLogOpt

#endif