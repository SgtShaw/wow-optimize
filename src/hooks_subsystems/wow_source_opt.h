#pragma once

// ============================================================================
// Module: wow_source_opt.h
// Description: Installs and manages target intercepts for subsystem `wow_source_opt.h`.
// Safety & Threading: Stack layouts and register conventions must match target function definitions exactly.
// ============================================================================


/**
 * @domain: Binary Detour Hooks Subsystem
 * @architecture: Detours target functions in `wow_source_opt.h` to bypass legacy bottlenecks.
 * @thread_affinity: Main Render Thread / Async Safe depending on sub-feature
 * @regression_hazard: Verify registers and stack layouts match target declarations exactly to prevent stack corruption.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */


#ifndef WOW_SOURCE_OPT_H
#define WOW_SOURCE_OPT_H

namespace WowSourceOpt {
    bool InstallAll();
    void ShutdownAll();
    void DumpStats();
}

#endif