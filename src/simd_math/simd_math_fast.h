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

// Vectorized Math Functions
void OptimizeSub82F0F0_VectorizedMath(float* inArray, float* outArray, int count);
void OptimizeSub6865B0_VectorizedTransform(float* inVec4, const float* matrix4x4, float* outVec4);
void OptimizeSub911FD0_VectorizedArrayScale(float* inArray, float scale, float* outArray, int count);
void OptimizeSub92F3C0_VectorizedMatrixMultiply(float* outMat, const float* inMatA, const float* inMatB);
float OptimizeSub8ECF60_BranchlessFloatAngle(float x, float y);
void OptimizeSub9AA210_VectorizedPositionLerp(const float* posA, const float* posB, float t, float* outPos);
void OptimizeSub92E790_VectorizedTransformMul(float* outMat, const float* inMatA, const float* inMatB);
void OptimizeSub579E50_VectorizedNormalRecalc(const float* inNormals, float* outNormals, int count);
int OptimizeSub91FCA0_BranchlessCharCmp(const char* strA, const char* strB);

} // namespace SimdMathFast
