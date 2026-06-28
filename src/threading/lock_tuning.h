#pragma once

// ============================================================================
// Module: lock_tuning.h
// Description: Supporting utility functions for `lock_tuning.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `lock_tuning.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Asynchronous Worker Thread Pools
 * @architecture: Manages lock-free task queuing and dispatches work asynchronously to worker threads.
 * @thread_affinity: Lock-Free Async Worker Pool
 * @regression_hazard: Thread synchronization issues or data race conditions will cause thread deadlocks or memory corruption.
 */



// Reduces lock-contention stalls by giving WoW's critical sections a userspace
// spin count. WoW's static MSVC CRT created its locks (heap, stdio, errno, ...)
// with InitializeCriticalSection -- spin count 0 -- so every contended acquisition
// is a kernel wait + context switch. On a many-core CPU a brief spin is far
// cheaper. Semantics are unchanged; only the spin-before-block behaviour differs.
bool InstallLockTuning();
void GetLockTuningStats(unsigned* retrofitted, unsigned* runtimeTuned);
