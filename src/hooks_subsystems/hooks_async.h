#pragma once

// ============================================================================
// Module: hooks_async.h
// Description: Installs and manages target intercepts for subsystem `hooks_async.h`.
// Safety & Threading: Stack layouts and register conventions must match target function definitions exactly.
// ============================================================================


/**
 * @domain: Binary Detour Hooks Subsystem
 * @architecture: Detours target functions in `hooks_async.h` to bypass legacy bottlenecks.
 * @thread_affinity: Main Render Thread / Async Safe depending on sub-feature
 * @regression_hazard: Verify registers and stack layouts match target declarations exactly to prevent stack corruption.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */



bool InstallAsyncHooks(void);
void ShutdownAsyncHooks(void);
void OnFrameAsyncHooks(DWORD mainThreadId);
