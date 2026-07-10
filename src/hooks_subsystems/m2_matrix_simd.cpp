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
}
