#pragma once

// ============================================================================
// Module: memory_pressure_governor.h
// Description: SSE2 vectorized replacement for legacy CRT function `memory_pressure_governor.h`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================


/**
 * @domain: Statically Linked CRT Replacement
 * @architecture: Replaces legacy x86 compiler intrinsic routines in `memory_pressure_governor.h` with vectorized SSE2.
 * @thread_affinity: Concurrent Execution Safe
 * @regression_hazard: Ensure page-boundary checks are active to prevent unaligned SSE reads from triggering page faults.
 */



/**
 * @domain: High-Performance Memory Allocations
 * @architecture: Overrides the standard CRT memory management callbacks using mimalloc redirects.
 * @thread_affinity: Worker Thread / Concurrent Execution Safe
 * @regression_hazard: Mismatched allocations between CRT heaps and mimalloc will cause instant heap corruption.
 */



// ================================================================
// Memory-Pressure Governor
// ================================================================
// Polls the HeapCompactor's cached LargestFreeBlock every frame,
// classifies VA pressure into 3 levels with hysteresis, and fires
// registered cache-shed callbacks + drops the texture budget toward
// stock under pressure, restoring on ease.
//
// No new thread or hook — driven from the existing Sleep hook.
// All shed actions are pure memset-to-zero; callers that rebuild
// on next access naturally refill when pressure eases.
// ================================================================

#include <windows.h>
#include <cstdint>

namespace PressureGovernor {

enum Level : int32_t {
    PRESSURE_GREEN  = 0,  // Normal — all caches full capacity
    PRESSURE_YELLOW = 1,  // Elevated — free < 48MB: shed non-critical
    PRESSURE_RED    = 2,  // Critical — free < 24MB: shed everything
};

// A shed-callback receives the new pressure level.  It should clear
// its cache if appropriate for that level and return.  Called from
// the main thread (Sleep hook), so plain stores are safe — no locks
// needed.
typedef void (*ShedCallback)(Level, void* ctx);

bool Init();
void Shutdown();

// Call every frame from the Sleep hook (guarded by IsReloading/IsSwapping).
void OnFrame();

// Current level for diagnostic/log consumers.
Level GetLevel();

// Register a shed callback. ctx is passed through; nullptr is fine.
// Up to 16 callbacks; returns false if the table is full.
bool RegisterShedCallback(ShedCallback cb, void* ctx);

} // namespace PressureGovernor