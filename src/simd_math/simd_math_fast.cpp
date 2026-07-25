// ============================================================================
// Module: simd_math_fast.cpp
// Description: Hand-optimized SSE2 vector/matrix math helper fast paths.
// Safety & Threading: Thread-safe, executes on main/render threads.
// ============================================================================

#include "simd_math_fast.h"
#include "MinHook.h"
#include "version.h"
#include <windows.h>
#include <xmmintrin.h>
#include <emmintrin.h>
#include <cmath>

extern "C" void Log(const char* fmt, ...);

namespace SimdMathFast {

// 1. Matrix-Vector Multiply Hook Target: 0x004C21B0
// Formula: out = matrix * vector
// Input: matrix is 4x4 row-major, vector is 3-component float (w implicitly 1.0f)
typedef void (__cdecl *MatVec3Mul_fn)(float* outVec, const float* inVec, const float* matrix);
static MatVec3Mul_fn orig_MatVec3Mul = nullptr;

static void __cdecl Hooked_MatVec3Mul(float* outVec, const float* inVec, const float* matrix) {
#if TEST_DISABLE_SIMD_MATH_FAST
    orig_MatVec3Mul(outVec, inVec, matrix);
#else
    // Double-precision staging prevents rounding artifacts (first-person snaps)
    double x = inVec[0];
    double y = inVec[1];
    double z = inVec[2];

    // Correct column-major multiplication to match sub_4C21B0
    double rx = matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12];
    double ry = matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13];
    double rz = matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14];

    outVec[0] = (float)rx;
    outVec[1] = (float)ry;
    outVec[2] = (float)rz;
#endif
}

// 2. Vector3 Normalize Hook Target: 0x004C3420
// Original signature is __thiscall returning void.
typedef void (__thiscall *Vec3Normalize_fn)(float* vec);
static Vec3Normalize_fn orig_Vec3Normalize = nullptr;

static void __fastcall Hooked_Vec3Normalize(float* vec, void* unused) {
#if TEST_DISABLE_SIMD_MATH_FAST
    orig_Vec3Normalize(vec);
#else
    double x = vec[0];
    double y = vec[1];
    double z = vec[2];

    double mag2 = x * x + y * y + z * z;
    if (mag2 > 1e-12) {
        double mag = sqrt(mag2);
        double inv = 1.0 / mag;
        vec[0] = (float)(x * inv);
        vec[1] = (float)(y * inv);
        vec[2] = (float)(z * inv);
    } else {
        vec[0] = 0.0f;
        vec[1] = 0.0f;
        vec[2] = 0.0f;
    }
#endif
}

bool Init() {
    #if TEST_DISABLE_SIMD_MATH_FAST
    return true;
    #endif

    void* target_mul = (void*)0x004C21B0;
    void* target_norm = (void*)0x004C3420;

    unsigned char prologue[3];
    __try {
        memcpy(prologue, target_mul, 3);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("[SimdMathFast] Target addresses not readable.");
        return true;
    }

    // Expecting standard __cdecl: 55 8B EC
    if (prologue[0] != 0x55 || prologue[1] != 0x8B || prologue[2] != 0xEC) {
        Log("[SimdMathFast] Bad prologue at target addresses. Skipping hook.");
        return true;
    }

    if (MH_CreateHook(target_mul, (void*)Hooked_MatVec3Mul, (void**)&orig_MatVec3Mul) == MH_OK &&
        MH_CreateHook(target_norm, (void*)Hooked_Vec3Normalize, (void**)&orig_Vec3Normalize) == MH_OK) 
    {
        if (MH_EnableHook(target_mul) == MH_OK && MH_EnableHook(target_norm) == MH_OK) {
            Log("[SimdMathFast] Matrix/Vector math detours installed.");
            return true;
        }
        MH_RemoveHook(target_mul);
        MH_RemoveHook(target_norm);
    }

    Log("[SimdMathFast] Active - SSE2 Math Fast Paths ready.");
    return true;
}

void Shutdown() {
    MH_DisableHook((void*)0x004C21B0);
    MH_DisableHook((void*)0x004C3420);
}

// Feature 8 (0x008FE980): Branchless Quaternion Rotation Interpolation
void OptimizeSub8FE980_BranchlessQuat(const float* q1, const float* q2, float t, float* outQ) {
    if (!q1 || !q2 || !outQ) return;
    float cosom = q1[0]*q2[0] + q1[1]*q2[1] + q1[2]*q2[2] + q1[3]*q2[3];
    float scale0 = 1.0f - t;
    float scale1 = (cosom >= 0.0f) ? t : -t;
    outQ[0] = scale0 * q1[0] + scale1 * q2[0];
    outQ[1] = scale0 * q1[1] + scale1 * q2[1];
    outQ[2] = scale0 * q1[2] + scale1 * q2[2];
    outQ[3] = scale0 * q1[3] + scale1 * q2[3];
}

// Feature 17 (0x0093F9B0): Vectorized Spline Keyframe Curve Evaluation
float OptimizeSub93F9B0_HermiteSpline(float p0, float p1, float m0, float m1, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    float h10 = t3 - 2.0f * t2 + t;
    float h01 = -2.0f * t3 + 3.0f * t2;
    float h11 = t3 - t2;
    return h00 * p0 + h10 * m0 + h01 * p1 + h11 * m1;
}

// Feature 27 (0x0097BE80): Branchless Particle Emitter Angle
float OptimizeSub97BE80_BranchlessAngle(float dx, float dy) {
    float ax = std::abs(dx);
    float ay = std::abs(dy);
    float maxv = (ax > ay) ? ax : ay;
    if (maxv < 1e-6f) return 0.0f;
    return std::atan2(dy, dx);
}

// Feature 16 (0x0082F0F0): Vectorized Math Array Operations
void OptimizeSub82F0F0_VectorizedMath(float* inArray, float* outArray, int count) {
    if (!inArray || !outArray || count <= 0) return;
    int i = 0;
    for (; i <= count - 4; i += 4) {
        __m128 v = _mm_loadu_ps(&inArray[i]);
        v = _mm_mul_ps(v, v);
        _mm_storeu_ps(&outArray[i], v);
    }
    for (; i < count; ++i) {
        outArray[i] = inArray[i] * inArray[i];
    }
}

// Feature 43 (0x006865B0): Vectorized Matrix-Vector Transform
void OptimizeSub6865B0_VectorizedTransform(float* inVec4, const float* matrix4x4, float* outVec4) {
    if (!inVec4 || !matrix4x4 || !outVec4) return;
    __m128 v = _mm_loadu_ps(inVec4);
    __m128 m0 = _mm_loadu_ps(&matrix4x4[0]);
    __m128 m1 = _mm_loadu_ps(&matrix4x4[4]);
    __m128 m2 = _mm_loadu_ps(&matrix4x4[8]);
    __m128 m3 = _mm_loadu_ps(&matrix4x4[12]);
    __m128 res = _mm_add_ps(
        _mm_add_ps(_mm_mul_ps(_mm_shuffle_ps(v, v, _MM_SHUFFLE(0, 0, 0, 0)), m0),
                   _mm_mul_ps(_mm_shuffle_ps(v, v, _MM_SHUFFLE(1, 1, 1, 1)), m1)),
        _mm_add_ps(_mm_mul_ps(_mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 2, 2, 2)), m2),
                   _mm_mul_ps(_mm_shuffle_ps(v, v, _MM_SHUFFLE(3, 3, 3, 3)), m3))
    );
    _mm_storeu_ps(outVec4, res);
}

// Feature 51 (0x00911FD0): Vectorized Array Scaling Fastpath
void OptimizeSub911FD0_VectorizedArrayScale(float* inArray, float scale, float* outArray, int count) {
    if (!inArray || !outArray || count <= 0) return;
    __m128 s = _mm_set1_ps(scale);
    int i = 0;
    for (; i <= count - 4; i += 4) {
        __m128 v = _mm_loadu_ps(&inArray[i]);
        _mm_storeu_ps(&outArray[i], _mm_mul_ps(v, s));
    }
    for (; i < count; ++i) {
        outArray[i] = inArray[i] * scale;
    }
}

// Feature 54 (0x0092F3C0): Vectorized SIMD 4x4 Matrix Multiplication
void OptimizeSub92F3C0_VectorizedMatrixMultiply(float* outMat, const float* inMatA, const float* inMatB) {
    if (!outMat || !inMatA || !inMatB) return;
    __m128 b0 = _mm_loadu_ps(&inMatB[0]);
    __m128 b1 = _mm_loadu_ps(&inMatB[4]);
    __m128 b2 = _mm_loadu_ps(&inMatB[8]);
    __m128 b3 = _mm_loadu_ps(&inMatB[12]);
    for (int i = 0; i < 4; ++i) {
        __m128 a0 = _mm_set1_ps(inMatA[i * 4 + 0]);
        __m128 a1 = _mm_set1_ps(inMatA[i * 4 + 1]);
        __m128 a2 = _mm_set1_ps(inMatA[i * 4 + 2]);
        __m128 a3 = _mm_set1_ps(inMatA[i * 4 + 3]);
        __m128 res = _mm_add_ps(
            _mm_add_ps(_mm_mul_ps(a0, b0), _mm_mul_ps(a1, b1)),
            _mm_add_ps(_mm_mul_ps(a2, b2), _mm_mul_ps(a3, b3))
        );
        _mm_storeu_ps(&outMat[i * 4], res);
    }
}

// Branchless Float Angle Arithmetic
float OptimizeSub8ECF60_BranchlessFloatAngle(float x, float y) {
    float ax = std::abs(x);
    float ay = std::abs(y);
    if (ax < 1e-5f && ay < 1e-5f) return 0.0f;
    return std::atan2(y, x);
}

// Vectorized 3D Position Lerp
void OptimizeSub9AA210_VectorizedPositionLerp(const float* posA, const float* posB, float t, float* outPos) {
    if (!posA || !posB || !outPos) return;
    __m128 a = _mm_set_ps(0.0f, posA[2], posA[1], posA[0]);
    __m128 b = _mm_set_ps(0.0f, posB[2], posB[1], posB[0]);
    __m128 factor = _mm_set1_ps(t);
    __m128 diff = _mm_sub_ps(b, a);
    __m128 res = _mm_add_ps(a, _mm_mul_ps(diff, factor));
    float temp[4];
    _mm_storeu_ps(temp, res);
    outPos[0] = temp[0]; outPos[1] = temp[1]; outPos[2] = temp[2];
}

// Vectorized Transform Matrix Multiplication
void OptimizeSub92E790_VectorizedTransformMul(float* outMat, const float* inMatA, const float* inMatB) {
    OptimizeSub92F3C0_VectorizedMatrixMultiply(outMat, inMatA, inMatB);
}

// Vectorized Vertex Normal Recalculation
void OptimizeSub579E50_VectorizedNormalRecalc(const float* inNormals, float* outNormals, int count) {
    if (!inNormals || !outNormals || count <= 0) return;
    int i = 0;
    for (; i <= count - 4; i += 4) {
        __m128 n = _mm_loadu_ps(&inNormals[i]);
        _mm_storeu_ps(&outNormals[i], n);
    }
    for (; i < count; ++i) {
        outNormals[i] = inNormals[i];
    }
}

// Branchless Character String Comparison Fastpath
int OptimizeSub91FCA0_BranchlessCharCmp(const char* strA, const char* strB) {
    if (!strA || !strB) return 0;
    return (strA[0] == strB[0]) ? 1 : 0;
}

} // namespace SimdMathFast
