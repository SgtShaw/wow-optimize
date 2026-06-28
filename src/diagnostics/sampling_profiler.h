#pragma once

// ============================================================================
// Module: sampling_profiler.h
// Description: Supporting utility functions for `sampling_profiler.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `sampling_profiler.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Memory Watchdog and Diagnostic Telemetry
 * @architecture: Implements minidump capture routines and watches CVars to log crash events.
 * @thread_affinity: Main Loop / Telemetry Threads
 * @regression_hazard: Exception handlers must not allocate memory during diagnostic recording to prevent nested exceptions.
 */



// ================================================================
// Sampling Profiler — lightweight in-DLL EIP sampler
// ================================================================
// A background thread samples the main thread's EIP every ~1ms via
// SuspendThread/GetThreadContext/ResumeThread, buckets each sample
// by the nearest known function, and dumps the top-N hot functions
// to the log on shutdown.
//
// This fixes the project's core blind spot: xrefs ≠ runtime frequency.
// Every future optimization becomes data-driven instead of a guess.
//
// Read-only sampling — no hooks into WoW code, no writes to WoW memory.
// Risk class: very low (same API family as crash dumpers use).
// ================================================================

#include <windows.h>
#include <cstdint>

namespace SamplingProfiler {

// Initialize the profiler. Stores the main thread handle for sampling.
// Call after the main thread ID is known (post-injection delay).
bool Init(HANDLE mainThread);

// Stop the sampling thread and dump results to the log.
// Call during DLL shutdown before closing handles.
void Shutdown();

// Returns true if the profiler is actively sampling.
bool IsActive();

// Get total number of samples collected (for diagnostics).
uint64_t GetSampleCount();

} // namespace SamplingProfiler