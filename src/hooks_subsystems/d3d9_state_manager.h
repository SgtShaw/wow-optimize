#pragma once

// ============================================================================
// Module: d3d9_state_manager.h
// Description: Supporting utility functions for `d3d9_state_manager.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `d3d9_state_manager.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */



bool InstallD3D9StateManager(void);
void ShutdownD3D9StateManager(void);
void OnFrameD3D9StateManager(DWORD mainThreadId);
bool IsD3D9DeviceHooked(void);

extern volatile LONG g_deviceResetCounter;
