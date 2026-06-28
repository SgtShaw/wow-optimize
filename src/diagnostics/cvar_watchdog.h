#pragma once

// ============================================================================
// Module: cvar_watchdog.h
// Description: Supporting utility functions for `cvar_watchdog.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `cvar_watchdog.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Memory Watchdog and Diagnostic Telemetry
 * @architecture: Implements minidump capture routines and watches CVars to log crash events.
 * @thread_affinity: Main Loop / Telemetry Threads
 * @regression_hazard: Exception handlers must not allocate memory during diagnostic recording to prevent nested exceptions.
 */


bool InitCvarWatchdog();
