#pragma once

// ============================================================================
// Module: simd_math_fast.h
// Description: Hand-optimized SSE2 vector/matrix math helper fast paths.
// Safety & Threading: Thread-safe, executes on main/render threads.
// ============================================================================

namespace SimdMathFast {

bool Init();
void Shutdown();

// Feature 8 (0x008FE980): Branchless Quaternion Rotation Interpolation
void OptimizeSub8FE980_BranchlessQuat(const float* q1, const float* q2, float t, float* outQ);

// Feature 17 (0x0093F9B0): Vectorized Spline Keyframe Curve Evaluation
float OptimizeSub93F9B0_HermiteSpline(float p0, float p1, float m0, float m1, float t);

// Feature 27 (0x0097BE80): Branchless Particle Emitter Angle
float OptimizeSub97BE80_BranchlessAngle(float dx, float dy);

} // namespace SimdMathFast
