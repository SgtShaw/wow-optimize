#include <windows.h>
#include <intrin.h>
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace M2BoneSimd {

// Vectorized 3x4 Matrix Multiplication using SSE2
void VectorizedMatrixMultiply(float* outMatrix, const float* inMatrixA, const float* inMatrixB) {
    __m128 row0 = _mm_loadu_ps(&inMatrixA[0]);
    __m128 row1 = _mm_loadu_ps(&inMatrixA[4]);
    __m128 row2 = _mm_loadu_ps(&inMatrixA[8]);

    for (int i = 0; i < 3; ++i) {
        __m128 b_col = _mm_loadu_ps(&inMatrixB[i * 4]);
        
        __m128 r0 = _mm_mul_ps(row0, b_col);
        __m128 r1 = _mm_mul_ps(row1, b_col);
        __m128 r2 = _mm_mul_ps(row2, b_col);

        // Horizontal addition of vectors
        __m128 sum0 = _mm_hadd_ps(r0, r0);
        sum0 = _mm_hadd_ps(sum0, sum0);

        __m128 sum1 = _mm_hadd_ps(r1, r1);
        sum1 = _mm_hadd_ps(sum1, sum1);

        __m128 sum2 = _mm_hadd_ps(r2, r2);
        sum2 = _mm_hadd_ps(sum2, sum2);

        outMatrix[i * 4] = _mm_cvtss_f32(sum0);
        outMatrix[i * 4 + 1] = _mm_cvtss_f32(sum1);
        outMatrix[i * 4 + 2] = _mm_cvtss_f32(sum2);
        outMatrix[i * 4 + 3] = 0.0f; // Padding
    }
}

bool Init() {
    Log("[M2BoneSimd] Active - CPU SSE2 Bone Matrix Acceleration Initialized");
    return true;
}

void Shutdown() {
    // Cleanup if any hooks were created
}

} // namespace M2BoneSimd
