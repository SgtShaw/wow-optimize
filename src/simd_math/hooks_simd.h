#pragma once

// ============================================================================
// Module: hooks_simd.h
// Description: Installs and manages target intercepts for subsystem `hooks_simd.h`.
// Safety & Threading: Stack layouts and register conventions must match target function definitions exactly.
// ============================================================================


/**
 * @domain: Binary Detour Hooks Subsystem
 * @architecture: Detours target functions in `hooks_simd.h` to bypass legacy bottlenecks.
 * @thread_affinity: Main Render Thread / Async Safe depending on sub-feature
 * @regression_hazard: Verify registers and stack layouts match target declarations exactly to prevent stack corruption.
 */



/**
 * @domain: Vectorized SIMD Math Operations
 * @architecture: Vectorizes legacy x87 FPU float operations using 128-bit SSE2 registers.
 * @thread_affinity: Main Loop / Worker Thread Safe
 * @regression_hazard: Unaligned vector load/store instructions or coordinate overlaps will cause memory violations or coordinate drift NaNs.
 */



bool InstallSimdHooks(void);
void ShutdownSimdHooks(void);

// SSE2 quaternion multiply: result = a * b (Hamilton product)
// All pointers are float[4] (x,y,z,w). Safe for aliasing.
void SSE2_QuatMultiply(const float* __restrict a, const float* __restrict b, float* __restrict result);

// SSE2 3-component dot product: returns a[0]*b[0] + a[1]*b[1] + a[2]*b[2]
float SSE2_Vec3Dot(const float* a, const float* b);