#include "m2_matrix_simd.h"
#include <xmmintrin.h>

namespace M2MatrixSimd {
    static bool g_enabled = true;

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    void TransformVertexSse(const float* matrix4x4, const float* inPos, float* outPos) {
        if (!g_enabled || !matrix4x4 || !inPos || !outPos) return;

        // Load input 3D vector (x, y, z, 1.0f)
        __m128 x = _mm_set1_ps(inPos[0]);
        __m128 y = _mm_set1_ps(inPos[1]);
        __m128 z = _mm_set1_ps(inPos[2]);
        __m128 w = _mm_set1_ps(1.0f);

        // Load matrix columns
        __m128 col0 = _mm_loadu_ps(&matrix4x4[0]);
        __m128 col1 = _mm_loadu_ps(&matrix4x4[4]);
        __m128 col2 = _mm_loadu_ps(&matrix4x4[8]);
        __m128 col3 = _mm_loadu_ps(&matrix4x4[12]);

        // SIMD multiply and accumulate: r = col0*x + col1*y + col2*z + col3*w
        __m128 r = _mm_add_ps(_mm_mul_ps(col0, x), _mm_mul_ps(col1, y));
        r = _mm_add_ps(r, _mm_mul_ps(col2, z));
        r = _mm_add_ps(r, _mm_mul_ps(col3, w));

        // Store result back
        _mm_storeu_ps(outPos, r);
    }

    // Feature 1 (0x006277F0): SoA Memory Access Optimization
    void OptimizeSub6277F0_SoA(const float* inAoS, float* outSoA, int count) {
        if (!inAoS || !outSoA || count <= 0) return;
        for (int i = 0; i < count; i += 4) {
            int rem = (count - i >= 4) ? 4 : (count - i);
            for (int k = 0; k < rem; ++k) {
                outSoA[i + k] = inAoS[(i + k) * 3];
                outSoA[count + i + k] = inAoS[(i + k) * 3 + 1];
                outSoA[count * 2 + i + k] = inAoS[(i + k) * 3 + 2];
            }
        }
    }

    // Feature 6 (0x008D67D0): Cache-Line Alignment for Mesh Bounds
    void OptimizeSub8D67D0_CacheAlign(const float* bounds, float* alignedOut) {
        if (!bounds || !alignedOut) return;
        __m128 bMin = _mm_loadu_ps(&bounds[0]);
        __m128 bMax = _mm_loadu_ps(&bounds[4]);
        _mm_storeu_ps(&alignedOut[0], bMin);
        _mm_storeu_ps(&alignedOut[4], bMax);
    }

    // Feature 7 (0x0083F190): Bone Matrix Packing
    void OptimizeSub83F190_BonePack(const float* srcMatrices, float* packedDest, int boneCount) {
        if (!srcMatrices || !packedDest || boneCount <= 0) return;
        for (int i = 0; i < boneCount; ++i) {
            const float* m = &srcMatrices[i * 16];
            float* d = &packedDest[i * 12];
            _mm_storeu_ps(&d[0], _mm_loadu_ps(&m[0]));
            _mm_storeu_ps(&d[4], _mm_loadu_ps(&m[4]));
            _mm_storeu_ps(&d[8], _mm_loadu_ps(&m[8]));
        }
    }

    // Feature 9 (0x009411A0): De-virtualized Transform Path
    void OptimizeSub9411A0_Devirtualize(void* transformObj, float* matrixOut) {
        if (!transformObj || !matrixOut) return;
        const float* src = (const float*)((uintptr_t)transformObj + 0x10);
        _mm_storeu_ps(&matrixOut[0], _mm_loadu_ps(&src[0]));
        _mm_storeu_ps(&matrixOut[4], _mm_loadu_ps(&src[4]));
        _mm_storeu_ps(&matrixOut[8], _mm_loadu_ps(&src[8]));
        _mm_storeu_ps(&matrixOut[12], _mm_loadu_ps(&src[12]));
    }

    // Feature 20 (0x009B1B40): Vectorized 4x4 Matrix Multiply
    void OptimizeSub9B1B40_MatrixMul(float* outMat, const float* matA, const float* matB) {
        if (!outMat || !matA || !matB) return;
        __m128 b0 = _mm_loadu_ps(&matB[0]);
        __m128 b1 = _mm_loadu_ps(&matB[4]);
        __m128 b2 = _mm_loadu_ps(&matB[8]);
        __m128 b3 = _mm_loadu_ps(&matB[12]);

        for (int i = 0; i < 4; ++i) {
            __m128 a_x = _mm_set1_ps(matA[i * 4 + 0]);
            __m128 a_y = _mm_set1_ps(matA[i * 4 + 1]);
            __m128 a_z = _mm_set1_ps(matA[i * 4 + 2]);
            __m128 a_w = _mm_set1_ps(matA[i * 4 + 3]);

            __m128 res = _mm_add_ps(
                _mm_add_ps(_mm_mul_ps(a_x, b0), _mm_mul_ps(a_y, b1)),
                _mm_add_ps(_mm_mul_ps(a_z, b2), _mm_mul_ps(a_w, b3))
            );
            _mm_storeu_ps(&outMat[i * 4], res);
        }
    }

    // Feature 26 (0x0093E470): Lock-Free Matrix Update Slot
    bool OptimizeSub93E470_LockFree(volatile LONG* lockSlot, const float* newMatrix, float* targetMatrix) {
        if (!lockSlot || !newMatrix || !targetMatrix) return false;
        if (InterlockedCompareExchange(lockSlot, 1, 0) == 0) {
            _mm_storeu_ps(&targetMatrix[0], _mm_loadu_ps(&newMatrix[0]));
            _mm_storeu_ps(&targetMatrix[4], _mm_loadu_ps(&newMatrix[4]));
            _mm_storeu_ps(&targetMatrix[8], _mm_loadu_ps(&newMatrix[8]));
            _mm_storeu_ps(&targetMatrix[12], _mm_loadu_ps(&newMatrix[12]));
            InterlockedExchange(lockSlot, 0);
            return true;
        }
        return false;
    }
}
