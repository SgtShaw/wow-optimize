#pragma once

// ============================================================================
// Module: addon_preload.h
// Description: Supporting utility functions for `addon_preload.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `addon_preload.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */


#include <windows.h>

// Init/shutdown
bool InitAddonPreload();
void ShutdownAddonPreload();
void ClearAddonPreload();

// Called from hooked_CreateFileA/W to track addon file handles
void AddonPreload_OnCreateFile(HANDLE hFile, const char* filename);
void AddonPreload_OnWriteFile(const char* filename);
bool AddonPreload_TryServe(HANDLE hFile, LPVOID lpBuffer, DWORD nBytes, LPDWORD lpBytesRead);
