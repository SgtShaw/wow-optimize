#pragma once

// ============================================================================
// Module: matrix_copy_sse2.h
// Description: Supporting utility functions for `matrix_copy_sse2.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `matrix_copy_sse2.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Vectorized SIMD Math Operations
 * @architecture: Vectorizes legacy x87 FPU float operations using 128-bit SSE2 registers.
 * @thread_affinity: Main Loop / Worker Thread Safe
 * @regression_hazard: Unaligned vector load/store instructions or coordinate overlaps will cause memory violations or coordinate drift NaNs.
 */



bool InstallMatrixCopySSE2();
void ShutdownMatrixCopySSE2();
