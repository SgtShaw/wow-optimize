#pragma once

// ============================================================================
// Module: font_metrics_fast.h
// Description: Supporting utility functions for `font_metrics_fast.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `font_metrics_fast.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */



#include <cstdint>

bool InstallFontMetricsFast();
void ShutdownFontMetricsFast();
void FontMetrics_OnFrame();
extern "C" void FontMetrics_GetStats(long* widthCalls, long* heightCalls);
