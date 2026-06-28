#pragma once

// ============================================================================
// Module: version_checker.h
// Description: Supporting utility functions for `version_checker.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `version_checker.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Kernel Operations and DLL Entry Orchestration
 * @architecture: Main DLL loading proxy logic and initialization routing via detours.
 * @thread_affinity: Main Thread Only
 * @regression_hazard: Mismatched registry initialization or early load sequence changes will cause system loader deadlocks.
 */


#include <windows.h>

bool VersionChecker_Init();
void VersionChecker_Shutdown();
bool VersionChecker_GetLatestVersion(char* out, size_t outLen);
