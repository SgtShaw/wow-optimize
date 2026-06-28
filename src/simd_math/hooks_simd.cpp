// ============================================================================
// Module: hooks_simd.cpp
// Description: Replaces legacy x87 FPU mathematics with vectorized SSE2 logic. Accelerates frustum culling (0x009839E0), quaternion slerp (0x00982460), normalization (0x00979110), and raycasting (0x009836B0).
// Safety & Threading: Main render thread. Staging inputs and outputs through local floats prevents memory aliasing under Release optimizations.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <cmath>
#include <xmmintrin.h>
#include <emmintrin.h>
#include "diagnostics/crash_dumper.h"
#include <tmmintrin.h>  // SSSE3 (_mm_shuffle_epi8 for color swizzle)
#include "MinHook.h"
#include "core/version.h"
#include "simd_math/hooks_simd.h"

extern "C" void Log(const char* fmt, ...);

// ================================================================
// SIMD Utility: 4x4 Matrix Multiply (SSE2)
// ================================================================
void SSE2_MatrixMultiply(const float* __restrict a,
                         const float* __restrict b,
                         float* __restrict result) {
    __m128 b0 = _mm_loadu_ps(b);       // b[0..3]
    __m128 b1 = _mm_loadu_ps(b + 4);   // b[4..7]
    __m128 b2 = _mm_loadu_ps(b + 8);   // b[8..11]
    __m128 b3 = _mm_loadu_ps(b + 12);  // b[12..15]

    for (int row = 0; row < 4; row++) {
        __m128 a_row = _mm_loadu_ps(a + row * 4); // a[row*4 .. row*4+3]

        __m128 r = _mm_mul_ps(_mm_shuffle_ps(a_row, a_row, _MM_SHUFFLE(0,0,0,0)), b0);
        r = _mm_add_ps(r, _mm_mul_ps(_mm_shuffle_ps(a_row, a_row, _MM_SHUFFLE(1,1,1,1)), b1));
        r = _mm_add_ps(r, _mm_mul_ps(_mm_shuffle_ps(a_row, a_row, _MM_SHUFFLE(2,2,2,2)), b2));
        r = _mm_add_ps(r, _mm_mul_ps(_mm_shuffle_ps(a_row, a_row, _MM_SHUFFLE(3,3,3,3)), b3));

        _mm_storeu_ps(result + row * 4, r);
    }
}

// ================================================================
// SIMD Utility: Quaternion Normalize (SSE2)
// ================================================================
static const float kQuatNormEps = 0.00000023841858f;

void SSE2_QuatNormalize(float* q) {
    __m128 v = _mm_loadu_ps(q); // x, y, z, w
    __m128 sq = _mm_mul_ps(v, v);

    __m128 shuf = _mm_shuffle_ps(sq, sq, _MM_SHUFFLE(2,3,0,1));
    __m128 sum1 = _mm_add_ps(sq, shuf);
    __m128 shuf2 = _mm_shuffle_ps(sum1, sum1, _MM_SHUFFLE(1,0,3,2));
    __m128 mag2 = _mm_add_ps(sum1, shuf2);  // full sum in every lane

    float m = mag2.m128_f32[0];
    if (!(m > kQuatNormEps)) return;

    __m128 inv = _mm_div_ss(_mm_set_ss(1.0f), _mm_sqrt_ss(mag2));
    __m128 invb = _mm_shuffle_ps(inv, inv, _MM_SHUFFLE(0,0,0,0));

    v = _mm_mul_ps(v, invb);

    float out_x = v.m128_f32[0];
    float out_y = v.m128_f32[1];
    float out_z = v.m128_f32[2];
    float out_w = v.m128_f32[3];

    q[0] = out_x;
    q[1] = out_y;
    q[2] = out_z;
    q[3] = out_w;
}

// ================================================================
// SIMD Utility: Quaternion Multiply (Hamilton product, SSE2)
// ================================================================
void SSE2_QuatMultiply(const float* __restrict a, const float* __restrict b, float* __restrict result) {
    __m128 va = _mm_loadu_ps(a); // ax, ay, az, aw
    __m128 vb = _mm_loadu_ps(b); // bx, by, bz, bw

    __m128 ax = _mm_shuffle_ps(va, va, _MM_SHUFFLE(0,0,0,0));
    __m128 ay = _mm_shuffle_ps(va, va, _MM_SHUFFLE(1,1,1,1));
    __m128 az = _mm_shuffle_ps(va, va, _MM_SHUFFLE(2,2,2,2));
    __m128 aw = _mm_shuffle_ps(va, va, _MM_SHUFFLE(3,3,3,3));

    __m128 bx = _mm_shuffle_ps(vb, vb, _MM_SHUFFLE(0,0,0,0));
    __m128 by = _mm_shuffle_ps(vb, vb, _MM_SHUFFLE(1,1,1,1));
    __m128 bz = _mm_shuffle_ps(vb, vb, _MM_SHUFFLE(2,2,2,2));
    __m128 bw = _mm_shuffle_ps(vb, vb, _MM_SHUFFLE(3,3,3,3));

    __m128 rw = _mm_sub_ps(_mm_sub_ps(_mm_mul_ps(aw, bw), _mm_mul_ps(ax, bx)),
                           _mm_add_ps(_mm_mul_ps(ay, by), _mm_mul_ps(az, bz)));
    __m128 rx = _mm_add_ps(_mm_add_ps(_mm_mul_ps(aw, bx), _mm_mul_ps(ax, bw)),
                           _mm_sub_ps(_mm_mul_ps(ay, bz), _mm_mul_ps(az, by)));
    __m128 ry = _mm_add_ps(_mm_sub_ps(_mm_mul_ps(aw, by), _mm_mul_ps(ax, bz)),
                           _mm_add_ps(_mm_mul_ps(ay, bw), _mm_mul_ps(az, bx)));
    __m128 rz = _mm_add_ps(_mm_sub_ps(_mm_mul_ps(aw, bz), _mm_mul_ps(ax, by)),
                           _mm_add_ps(_mm_mul_ps(az, bw), _mm_mul_ps(ay, bx)));

    float fx, fy, fz, fw;
    _mm_store_ss(&fx, rx);
    _mm_store_ss(&fy, ry);
    _mm_store_ss(&fz, rz);
    _mm_store_ss(&fw, rw);
    result[0] = fx; result[1] = fy; result[2] = fz; result[3] = fw;
}

// ================================================================
// SIMD Utility: 3-Component Dot Product (SSE2)
// ================================================================
float SSE2_Vec3Dot(const float* a, const float* b) {
    __m128 va = _mm_setr_ps(a[0], a[1], a[2], 0.0f);
    __m128 vb = _mm_setr_ps(b[0], b[1], b[2], 0.0f);
    __m128 m = _mm_mul_ps(va, vb);
    __m128 s1 = _mm_add_ss(m, _mm_shuffle_ps(m, m, _MM_SHUFFLE(1,1,1,1)));
    __m128 s2 = _mm_add_ss(s1, _mm_shuffle_ps(m, m, _MM_SHUFFLE(2,2,2,2)));
    float r;
    _mm_store_ss(&r, s2);
    return r;
}

// ================================================================
// SIMD Utility: Vector3 Normalize (SSE2)
// ================================================================
void SSE2_Vec3Normalize(float* __restrict v) {
    __m128 xyz = _mm_loadu_ps(v);
    __m128 sq  = _mm_mul_ps(xyz, xyz);

    __m128 shuf = _mm_shuffle_ps(sq, sq, _MM_SHUFFLE(2,3,0,1));
    __m128 sum1 = _mm_add_ps(sq, shuf);
    __m128 sum2 = _mm_add_ss(sum1, _mm_shuffle_ps(sum1, sum1, _MM_SHUFFLE(1,1,1,1)));

    __m128 rlen = _mm_rsqrt_ss(sum2);

    __m128 half = _mm_set_ss(0.5f);
    __m128 three = _mm_set_ss(3.0f);
    __m128 rlen2 = _mm_mul_ss(sum2, _mm_mul_ss(rlen, rlen));
    rlen = _mm_mul_ss(_mm_mul_ss(half, rlen), _mm_sub_ss(three, rlen2));

    rlen = _mm_shuffle_ps(rlen, rlen, _MM_SHUFFLE(0,0,0,0));
    xyz = _mm_mul_ps(xyz, rlen);
    _mm_storeu_ps(v, xyz);
}

// ================================================================
// Frustum Culling SIMD Implementation
// ================================================================
struct SSEPlane {
    __m128 normal_d; // nx, ny, nz, d
};

struct SSEAABB {
    __m128 min;  // minX, minY, minZ, 0
    __m128 max;  // maxX, maxY, maxZ, 0
};

static int SSE2_FrustumCull4(const SSEAABB* aabb, const SSEPlane* planes) {
    __m128 bbMin = aabb->min;
    __m128 bbMax = aabb->max;

    for (int p = 0; p < 4; p++) {
        __m128 plane = planes[p].normal_d;
        __m128 normal = plane;
        __m128 posMask = _mm_cmpge_ps(normal, _mm_setzero_ps());
        __m128 nVertex = _mm_or_ps(_mm_and_ps(posMask, bbMax), _mm_andnot_ps(posMask, bbMin));

        __m128 dotN = _mm_mul_ps(nVertex, plane);
        __m128 shuf1 = _mm_shuffle_ps(dotN, dotN, _MM_SHUFFLE(2,3,0,1));
        __m128 sum1  = _mm_add_ps(dotN, shuf1);
        __m128 sum2  = _mm_add_ss(sum1, _mm_shuffle_ps(sum1, sum1, _MM_SHUFFLE(1,1,1,1)));
        sum2 = _mm_add_ss(sum2, _mm_shuffle_ps(plane, plane, _MM_SHUFFLE(3,3,3,3)));

        float dist;
        _mm_store_ss(&dist, sum2);

        if (dist < 0.0f) {
            return 0; // Culled
        }
    }
    return 1;
}

int SSE2_FrustumCull6(const float aabbMin[3], const float aabbMax[3],
                      const float frustumPlanes[6][4]) {
    SSEAABB aabb;
    aabb.min = _mm_setr_ps(aabbMin[0], aabbMin[1], aabbMin[2], 0.0f);
    aabb.max = _mm_setr_ps(aabbMax[0], aabbMax[1], aabbMax[2], 0.0f);

    SSEPlane planes4[4];
    for (int i = 0; i < 4; i++) {
        planes4[i].normal_d = _mm_loadu_ps(frustumPlanes[i]);
    }
    if (!SSE2_FrustumCull4(&aabb, planes4)) return 0;

    SSEPlane planes2[4];
    for (int i = 0; i < 2; i++) {
        planes2[i].normal_d = _mm_loadu_ps(frustumPlanes[4 + i]);
    }
    planes2[2].normal_d = _mm_setr_ps(0.0f, 0.0f, 1.0f, 1e10f);
    planes2[3].normal_d = _mm_setr_ps(0.0f, 0.0f, 1.0f, 1e10f);

    return SSE2_FrustumCull4(&aabb, planes2);
}

// ================================================================
// Color/Alpha Batch SIMD Conversion
// ================================================================
void SSE2_BGRAtoARGB_Batch(const uint8_t* __restrict src,
                           uint8_t* __restrict dst,
                           size_t pixelCount) {
    const __m128i bMask = _mm_setr_epi8(2,1,0,3, 6,5,4,7, 10,9,8,11, 14,13,12,15);

    size_t vecCount = pixelCount / 4;
    for (size_t i = 0; i < vecCount; i++) {
        __m128i pixel4 = _mm_loadu_si128((const __m128i*)(src + i * 16));
        __m128i swapped = _mm_shuffle_epi8(pixel4, bMask);
        _mm_storeu_si128((__m128i*)(dst + i * 16), swapped);
    }

    for (size_t i = vecCount * 4; i < pixelCount; i++) {
        uint8_t b = src[i * 4 + 0];
        uint8_t g = src[i * 4 + 1];
        uint8_t r = src[i * 4 + 2];
        uint8_t a = src[i * 4 + 3];
        dst[i * 4 + 0] = r;
        dst[i * 4 + 1] = g;
        dst[i * 4 + 2] = b;
        dst[i * 4 + 3] = a;
    }
}

void SSE2_PremultiplyAlpha_Batch(const uint8_t* __restrict src,
                                  uint8_t* __restrict dst,
                                  size_t pixelCount) {
    const __m128i alphaMask = _mm_setr_epi8(
        3,3,3,3,  7,7,7,7,  11,11,11,11,  15,15,15,15);
    const __m128i half = _mm_set1_epi16(128);
    const __m128i zero = _mm_setzero_si128();

    size_t vecCount = pixelCount / 4;
    for (size_t i = 0; i < vecCount; i++) {
        __m128i pixel4 = _mm_loadu_si128((const __m128i*)(src + i * 16));
        __m128i alpha = _mm_shuffle_epi8(pixel4, alphaMask);

        __m128i pxLo = _mm_unpacklo_epi8(pixel4, zero);
        __m128i pxHi = _mm_unpackhi_epi8(pixel4, zero);
        __m128i alLo = _mm_unpacklo_epi8(alpha, zero);
        __m128i alHi = _mm_unpackhi_epi8(alpha, zero);

        pxLo = _mm_mullo_epi16(pxLo, alLo);
        pxHi = _mm_mullo_epi16(pxHi, alHi);

        pxLo = _mm_srli_epi16(_mm_add_epi16(pxLo, half), 8);
        pxHi = _mm_srli_epi16(_mm_add_epi16(pxHi, half), 8);

        pixel4 = _mm_packus_epi16(pxLo, pxHi);

        const __m128i rgbMask = _mm_setr_epi8(
            (char)0xFF,(char)0xFF,(char)0xFF,0, (char)0xFF,(char)0xFF,(char)0xFF,0,
            (char)0xFF,(char)0xFF,(char)0xFF,0, (char)0xFF,(char)0xFF,(char)0xFF,0);
        pixel4 = _mm_or_si128(_mm_and_si128(pixel4, rgbMask),
                               _mm_andnot_si128(rgbMask, _mm_loadu_si128((const __m128i*)(src + i * 16))));

        _mm_storeu_si128((__m128i*)(dst + i * 16), pixel4);
    }

    for (size_t i = vecCount * 4; i < pixelCount; i++) {
        uint8_t r = src[i * 4 + 0];
        uint8_t g = src[i * 4 + 1];
        uint8_t b = src[i * 4 + 2];
        uint8_t a = src[i * 4 + 3];
        dst[i * 4 + 0] = (uint8_t)(((uint16_t)r * a + 128) >> 8);
        dst[i * 4 + 1] = (uint8_t)(((uint16_t)g * a + 128) >> 8);
        dst[i * 4 + 2] = (uint8_t)(((uint16_t)b * a + 128) >> 8);
        dst[i * 4 + 3] = a;
    }
}

void SSE2_Vec3Cross(const float* __restrict a,
                    const float* __restrict b,
                    float* __restrict result) {
    __m128 va = _mm_loadu_ps(a);
    __m128 vb = _mm_loadu_ps(b);

    __m128 a_yzx = _mm_shuffle_ps(va, va, _MM_SHUFFLE(3,0,2,1));
    __m128 b_yzx = _mm_shuffle_ps(vb, vb, _MM_SHUFFLE(3,0,2,1));
    __m128 a_zxy = _mm_shuffle_ps(va, va, _MM_SHUFFLE(3,1,0,2));
    __m128 b_zxy = _mm_shuffle_ps(vb, vb, _MM_SHUFFLE(3,1,0,2));

    __m128 cross = _mm_sub_ps(_mm_mul_ps(a_yzx, b_zxy),
                               _mm_mul_ps(a_zxy, b_yzx));

    _mm_storeu_ps(result, cross);
}

#ifndef ADDR_WOW_MATRIX_MULTIPLY
#define ADDR_WOW_MATRIX_MULTIPLY 0x00000000
#endif
#ifndef ADDR_WOW_QUAT_NORMALIZE
#define ADDR_WOW_QUAT_NORMALIZE 0x00979110
#endif
#ifndef ADDR_WOW_FRUSTUM_CULL
#define ADDR_WOW_FRUSTUM_CULL  0x009839E0
#endif
#ifndef ADDR_WOW_FRUSTUM_CULL_TYPE2
#define ADDR_WOW_FRUSTUM_CULL_TYPE2 0x00983A60
#endif
#ifndef ADDR_WOW_FRUSTUM_CULL_POINT
#define ADDR_WOW_FRUSTUM_CULL_POINT 0x00983D70
#endif
#ifndef ADDR_WOW_RAY_TRIANGLE_32BIT
#define ADDR_WOW_RAY_TRIANGLE_32BIT 0x009836B0
#endif
#ifndef ADDR_WOW_RAY_TRIANGLE_16BIT
#define ADDR_WOW_RAY_TRIANGLE_16BIT 0x00983490
#endif

// ================================================================
// Statistics & Active State
// ================================================================
static volatile LONG64 g_matMulCalls    = 0;
static volatile LONG64 g_quatNormCalls  = 0;
static volatile LONG64 g_frustumCalls   = 0;
static volatile LONG64 g_frustumCulled  = 0;
static float g_activeFrustum[24] = {0};
static bool g_hasActiveFrustum = false;
static uint32_t g_particleFrameCount = 0;
static volatile LONG64 g_rayTriangleCalls = 0;
static volatile LONG64 g_rayTriangleIntersects = 0;

// ================================================================
// Public APIs
// ================================================================

int SSE2_IsAABBVisible(const float* planes, const float* bounds) {
    __try {
        if (!planes || !bounds) return 3;
        
        // 4GB address space boundary audit fixes (LAA compatibility)
        if ((uintptr_t)planes < 0x10000 || (uintptr_t)planes >= 0xFFE00000) return 3;
        if ((uintptr_t)bounds < 0x10000 || (uintptr_t)bounds >= 0xFFE00000) return 3;

        __m128 min_x = _mm_set1_ps(bounds[0]);
        __m128 min_y = _mm_set1_ps(bounds[1]);
        __m128 min_z = _mm_set1_ps(bounds[2]);
        __m128 max_x = _mm_set1_ps(bounds[3]);
        __m128 max_y = _mm_set1_ps(bounds[4]);
        __m128 max_z = _mm_set1_ps(bounds[5]);

        const __m128 eps = _mm_set1_ps(-0.019444443f);

        __m128 r0 = _mm_loadu_ps(planes + 0);
        __m128 r1 = _mm_loadu_ps(planes + 4);
        __m128 r2 = _mm_loadu_ps(planes + 8);
        __m128 r3 = _mm_loadu_ps(planes + 12);

        _MM_TRANSPOSE4_PS(r0, r1, r2, r3);

        __m128 mask_x = _mm_cmpge_ps(r0, _mm_setzero_ps());
        __m128 x_val = _mm_or_ps(_mm_and_ps(mask_x, max_x), _mm_andnot_ps(mask_x, min_x));

        __m128 mask_y = _mm_cmpge_ps(r1, _mm_setzero_ps());
        __m128 y_val = _mm_or_ps(_mm_and_ps(mask_y, max_y), _mm_andnot_ps(mask_y, min_y));

        __m128 mask_z = _mm_cmpge_ps(r2, _mm_setzero_ps());
        __m128 z_val = _mm_or_ps(_mm_and_ps(mask_z, max_z), _mm_andnot_ps(mask_z, min_z));

        __m128 dp = _mm_add_ps(_mm_add_ps(_mm_mul_ps(r0, x_val), _mm_mul_ps(r1, y_val)), 
                               _mm_add_ps(_mm_mul_ps(r2, z_val), r3));

        __m128 cull_mask = _mm_cmplt_ps(dp, eps);
        if (_mm_movemask_ps(cull_mask) != 0) {
            return 0;
        }

        __m128 r4 = _mm_loadu_ps(planes + 16);
        __m128 r5 = _mm_loadu_ps(planes + 20);
        __m128 r6 = _mm_set_ps(1000.0f, 0.0f, 0.0f, 0.0f);
        __m128 r7 = _mm_set_ps(1000.0f, 0.0f, 0.0f, 0.0f);

        _MM_TRANSPOSE4_PS(r4, r5, r6, r7);

        __m128 mask_x2 = _mm_cmpge_ps(r4, _mm_setzero_ps());
        __m128 x_val2 = _mm_or_ps(_mm_and_ps(mask_x2, max_x), _mm_andnot_ps(mask_x2, min_x));

        __m128 mask_y2 = _mm_cmpge_ps(r5, _mm_setzero_ps());
        __m128 y_val2 = _mm_or_ps(_mm_and_ps(mask_y2, max_y), _mm_andnot_ps(mask_y2, min_y));

        __m128 mask_z2 = _mm_cmpge_ps(r6, _mm_setzero_ps());
        __m128 z_val2 = _mm_or_ps(_mm_and_ps(mask_z2, max_z), _mm_andnot_ps(mask_z2, min_z));

        __m128 dp2 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(r4, x_val2), _mm_mul_ps(r5, y_val2)), 
                                _mm_add_ps(_mm_mul_ps(r6, z_val2), r7));

        __m128 cull_mask2 = _mm_cmplt_ps(dp2, eps);
        if (_mm_movemask_ps(cull_mask2) != 0) {
            return 0;
        }

        return 3;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 3;
    }
}

int SSE2_IsAABBVisible_Type2(const float* planes, const float* bounds) {
    __try {
        if (!planes || !bounds) return 3;
        
        // 4GB address space boundary audit fixes (LAA compatibility)
        if ((uintptr_t)planes < 0x10000 || (uintptr_t)planes >= 0xFFE00000) return 3;
        if ((uintptr_t)bounds < 0x10000 || (uintptr_t)bounds >= 0xFFE00000) return 3;

        __m128 min_x = _mm_set1_ps(bounds[0]);
        __m128 min_y = _mm_set1_ps(bounds[1]);
        __m128 min_z = _mm_set1_ps(bounds[2]);
        __m128 max_x = _mm_set1_ps(bounds[3]);
        __m128 max_y = _mm_set1_ps(bounds[4]);
        __m128 max_z = _mm_set1_ps(bounds[5]);

        const __m128 eps = _mm_set1_ps(0.019444443f);

        __m128 r0 = _mm_loadu_ps(planes + 0);
        __m128 r1 = _mm_loadu_ps(planes + 4);
        __m128 r2 = _mm_loadu_ps(planes + 8);
        __m128 r3 = _mm_loadu_ps(planes + 12);

        _MM_TRANSPOSE4_PS(r0, r1, r2, r3);

        __m128 mask_x = _mm_cmpge_ps(r0, _mm_setzero_ps());
        __m128 x_val = _mm_or_ps(_mm_and_ps(mask_x, max_x), _mm_andnot_ps(mask_x, min_x));

        __m128 mask_y = _mm_cmpge_ps(r1, _mm_setzero_ps());
        __m128 y_val = _mm_or_ps(_mm_and_ps(mask_y, max_y), _mm_andnot_ps(mask_y, min_y));

        __m128 mask_z = _mm_cmpge_ps(r2, _mm_setzero_ps());
        __m128 z_val = _mm_or_ps(_mm_and_ps(mask_z, max_z), _mm_andnot_ps(mask_z, min_z));

        __m128 dp = _mm_add_ps(_mm_add_ps(_mm_mul_ps(r0, x_val), _mm_mul_ps(r1, y_val)), 
                               _mm_add_ps(_mm_mul_ps(r2, z_val), r3));

        __m128 cull_mask = _mm_cmpgt_ps(dp, eps);
        if (_mm_movemask_ps(cull_mask) != 0) {
            return 0;
        }

        __m128 r4 = _mm_loadu_ps(planes + 16);
        __m128 r5 = _mm_loadu_ps(planes + 20);
        __m128 r6 = _mm_set_ps(-1000.0f, 0.0f, 0.0f, 0.0f);
        __m128 r7 = _mm_set_ps(-1000.0f, 0.0f, 0.0f, 0.0f);

        _MM_TRANSPOSE4_PS(r4, r5, r6, r7);

        __m128 mask_x2 = _mm_cmpge_ps(r4, _mm_setzero_ps());
        __m128 x_val2 = _mm_or_ps(_mm_and_ps(mask_x2, max_x), _mm_andnot_ps(mask_x2, min_x));

        __m128 mask_y2 = _mm_cmpge_ps(r5, _mm_setzero_ps());
        __m128 y_val2 = _mm_or_ps(_mm_and_ps(mask_y2, max_y), _mm_andnot_ps(mask_y2, min_y));

        __m128 mask_z2 = _mm_cmpge_ps(r6, _mm_setzero_ps());
        __m128 z_val2 = _mm_or_ps(_mm_and_ps(mask_z2, max_z), _mm_andnot_ps(mask_z2, min_z));

        __m128 dp2 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(r4, x_val2), _mm_mul_ps(r5, y_val2)), 
                                _mm_add_ps(_mm_mul_ps(r6, z_val2), r7));

        __m128 cull_mask2 = _mm_cmpgt_ps(dp2, eps);
        if (_mm_movemask_ps(cull_mask2) != 0) {
            return 0;
        }

        return 3;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 3;
    }
}

void SSE2_IsPointVisible(const float* planes, const float* point, uint8_t* outMask) {
    __try {
        if (!planes || !point || !outMask) return;
        
        // 4GB address space boundary audit fixes (LAA compatibility)
        if ((uintptr_t)planes < 0x10000 || (uintptr_t)planes >= 0xFFE00000) return;
        if ((uintptr_t)point < 0x10000 || (uintptr_t)point >= 0xFFE00000) return;
        if ((uintptr_t)outMask < 0x10000 || (uintptr_t)outMask >= 0xFFE00000) return;

        __m128 px = _mm_set1_ps(point[0]);
        __m128 py = _mm_set1_ps(point[1]);
        __m128 pz = _mm_set1_ps(point[2]);
        const __m128 eps = _mm_set1_ps(-0.019444443f);

        __m128 r0 = _mm_loadu_ps(planes + 0);
        __m128 r1 = _mm_loadu_ps(planes + 4);
        __m128 r2 = _mm_loadu_ps(planes + 8);
        __m128 r3 = _mm_loadu_ps(planes + 12);

        _MM_TRANSPOSE4_PS(r0, r1, r2, r3);

        __m128 dp = _mm_add_ps(_mm_add_ps(_mm_mul_ps(r0, px), _mm_mul_ps(r1, py)), 
                               _mm_add_ps(_mm_mul_ps(r2, pz), r3));

        __m128 mask = _mm_cmplt_ps(dp, eps);
        int bitmask1 = _mm_movemask_ps(mask);

        __m128 r4 = _mm_loadu_ps(planes + 16);
        __m128 r5 = _mm_loadu_ps(planes + 20);
        __m128 r6 = _mm_setzero_ps();
        __m128 r7 = _mm_setzero_ps();

        _MM_TRANSPOSE4_PS(r4, r5, r6, r7);

        __m128 dp2 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(r4, px), _mm_mul_ps(r5, py)), 
                                _mm_add_ps(_mm_mul_ps(r6, pz), r7));

        __m128 mask2 = _mm_cmplt_ps(dp2, eps);
        int bitmask2 = _mm_movemask_ps(mask2) & 3;

        *outMask = (uint8_t)(bitmask1 | (bitmask2 << 4));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static inline __m128 SSE2_Cross(const __m128& a, const __m128& b) {
    __m128 a_1 = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 0, 2, 1));
    __m128 b_1 = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 1, 0, 2));
    __m128 a_2 = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 1, 0, 2));
    __m128 b_2 = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 0, 2, 1));
    return _mm_sub_ps(_mm_mul_ps(a_1, b_1), _mm_mul_ps(a_2, b_2));
}

static inline __m128 SSE2_Dot3_Val(const __m128& a, const __m128& b) {
    __m128 m = _mm_mul_ps(a, b);
    __m128 shuf1 = _mm_shuffle_ps(m, m, _MM_SHUFFLE(2, 3, 0, 1));
    __m128 sum1 = _mm_add_ps(m, shuf1);
    __m128 shuf2 = _mm_shuffle_ps(sum1, sum1, _MM_SHUFFLE(0, 0, 2, 2));
    return _mm_add_ps(sum1, shuf2);
}

template <typename IndexType>
static inline char SSE2_RayTriangleIntersection(const float* ray, const float* vertices, const IndexType* indices, float* outT, float* outUV, float margin, char (__cdecl* orig_fn)(const float*, const float*, const IndexType*, float*, float*, float)) {
    __try {
        if (!ray || !vertices || !indices) {
            if (orig_fn) return orig_fn(ray, vertices, indices, outT, outUV, margin);
            return 0;
        }
        
        // 4GB address space boundary audit fixes (LAA compatibility)
        if ((uintptr_t)ray < 0x10000 || (uintptr_t)ray >= 0xFFE00000 ||
            (uintptr_t)vertices < 0x10000 || (uintptr_t)vertices >= 0xFFE00000 ||
            (uintptr_t)indices < 0x10000 || (uintptr_t)indices >= 0xFFE00000 ||
            (outT && ((uintptr_t)outT < 0x10000 || (uintptr_t)outT >= 0xFFE00000)) ||
            (outUV && ((uintptr_t)outUV < 0x10000 || (uintptr_t)outUV >= 0xFFE00000))) {
            if (orig_fn) return orig_fn(ray, vertices, indices, outT, outUV, margin);
            return 0;
        }

        IndexType idx0 = indices[0];
        IndexType idx1 = indices[1];
        IndexType idx2 = indices[2];

        const float* v0 = vertices + 3 * idx0;
        const float* v1 = vertices + 3 * idx1;
        const float* v2 = vertices + 3 * idx2;

        if ((uintptr_t)v0 < 0x10000 || (uintptr_t)v0 >= 0xFFE00000 ||
            (uintptr_t)v1 < 0x10000 || (uintptr_t)v1 >= 0xFFE00000 ||
            (uintptr_t)v2 < 0x10000 || (uintptr_t)v2 >= 0xFFE00000) {
            if (orig_fn) return orig_fn(ray, vertices, indices, outT, outUV, margin);
            return 0;
        }

        __m128 origin = _mm_set_ps(0.0f, ray[2], ray[1], ray[0]);
        __m128 dir = _mm_set_ps(0.0f, ray[5], ray[4], ray[3]);

        __m128 pt0 = _mm_set_ps(0.0f, v0[2], v0[1], v0[0]);
        __m128 pt1 = _mm_set_ps(0.0f, v1[2], v1[1], v1[0]);
        __m128 pt2 = _mm_set_ps(0.0f, v2[2], v2[1], v2[0]);

        __m128 edge1 = _mm_sub_ps(pt1, pt0);
        __m128 edge2 = _mm_sub_ps(pt2, pt0);

        __m128 pvec = SSE2_Cross(dir, edge2);
        __m128 det_v = SSE2_Dot3_Val(edge1, pvec);

        float det;
        _mm_store_ss(&det, det_v);

        if (det > -0.000001f && det < 0.000001f) {
            return 0;
        }

        __m128 inv_det_v = _mm_set1_ps(1.0f);
        inv_det_v = _mm_div_ps(inv_det_v, det_v);

        __m128 tvec = _mm_sub_ps(origin, pt0);
        __m128 u_v = _mm_mul_ps(SSE2_Dot3_Val(tvec, pvec), inv_det_v);

        float u;
        _mm_store_ss(&u, u_v);

        float min_val = -margin;
        float max_val = margin + 1.0f;

        if (u < min_val || u > max_val) {
            return 0;
        }

        __m128 qvec = SSE2_Cross(tvec, edge1);
        __m128 v_v = _mm_mul_ps(SSE2_Dot3_Val(dir, qvec), inv_det_v);

        float v;
        _mm_store_ss(&v, v_v);

        if (v < min_val || (u + v) > max_val) {
            return 0;
        }

        if (outT) {
            __m128 t_val = _mm_mul_ps(SSE2_Dot3_Val(edge2, qvec), inv_det_v);
            _mm_store_ss(outT, t_val);
        }

        if (outUV) {
            outUV[0] = u;
            outUV[1] = v;
        }

        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (orig_fn) return orig_fn(ray, vertices, indices, outT, outUV, margin);
        return 0;
    }
}

#if !TEST_DISABLE_RAY_TRIANGLE_SSE2
typedef char (__cdecl *RayTriangle32_t)(const float* ray, const float* vertices, const uint32_t* indices, float* outT, float* outUV, float margin);
static RayTriangle32_t orig_RayTriangle32 = nullptr;

/**
 * @target_address: 0x009836B0
 * @rationale: Accelerates raycasting triangle intersection lookups for world geometry collisions.
 * @calling_convention: __cdecl (floats, pointers, margin passed via stack)
 * @thread_affinity: Worker Thread Safe (invoked during asynchronous geometry collision queries)
 * @regression_hazard: Do not omit bounds checking on input float buffers. Under large address space
 *                     mapping states, high heap boundaries exceeding 3GB can trigger silent page faults
 *                     if not correctly masked up to 0xFFE00000.
 */
static char __cdecl Hooked_RayTriangle32(const float* ray, const float* vertices, const uint32_t* indices, float* outT, float* outUV, float margin) {
    g_rayTriangleCalls++;
    char res = SSE2_RayTriangleIntersection<uint32_t>(ray, vertices, indices, outT, outUV, margin, orig_RayTriangle32);
    if (res) {
        g_rayTriangleIntersects++;
    }
    return res;
}

typedef char (__cdecl *RayTriangle16_t)(const float* ray, const float* vertices, const uint16_t* indices, float* outT, float* outUV, float margin);
static RayTriangle16_t orig_RayTriangle16 = nullptr;

/**
 * @target_address: 0x00983490
 * @rationale: Accelerates raycasting lookups for 16-bit indexed mesh segments.
 * @calling_convention: __cdecl (margin and buffer pointers passed via stack)
 * @thread_affinity: Worker Thread Safe (invoked asynchronously by terrain and physics systems)
 * @regression_hazard: Stack frames must not be polluted. Ensure index values are read within
 *                     validated page limits to prevent out-of-bounds vertex index lookup crashes.
 */
static char __cdecl Hooked_RayTriangle16(const float* ray, const float* vertices, const uint16_t* indices, float* outT, float* outUV, float margin) {
    g_rayTriangleCalls++;
    char res = SSE2_RayTriangleIntersection<uint16_t>(ray, vertices, indices, outT, outUV, margin, orig_RayTriangle16);
    if (res) {
        g_rayTriangleIntersects++;
    }
    return res;
}
#endif

#if !TEST_DISABLE_QUAT_NORMALIZE
typedef void (__fastcall *QuatNormalize_t)(float* ecx, void* edx);
static QuatNormalize_t orig_QuatNormalize = nullptr;

/**
 * @target_address: 0x00979110
 * @rationale: Replaces scalar x87 quaternion normalization inside animation blends with fast SSE2.
 * @calling_convention: __fastcall (Workaround for __thiscall: ECX contains pointer to quaternion)
 * @thread_affinity: Main Thread execution only (invoked during frame render updates)
 * @regression_hazard: Do not replace full-precision sqrtss and divss with reciprocal approximations (rsqrtss).
 *                     Approximation drift will poison rotation matrices, inducing mesh scale collapses and NaN cameras.
 */
static void __fastcall Hooked_QuatNormalize(float* ecx, void* edx) {
    g_quatNormCalls++;
    uintptr_t p = (uintptr_t)ecx;
    if (p > 0x10000 && p < 0xFFE00000) {
        __try {
            SSE2_QuatNormalize(ecx);
            return;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (orig_QuatNormalize) orig_QuatNormalize(ecx, edx);
}
#endif

#if !TEST_DISABLE_FRUSTUM_CULL
typedef int (__fastcall *IsAABBVisible_t)(void* ecx, void* edx, const float* bounds);
static IsAABBVisible_t orig_IsAABBVisible = nullptr;

/**
 * @target_address: 0x009839E0
 * @rationale: Speeds up geometry culling by testing object AABBs against 4 frustum planes at once.
 * @calling_convention: __fastcall (Workaround for __thiscall: ECX holds planes structure, bounds passed on stack)
 * @thread_affinity: Main Render Thread Only
 * @regression_hazard: Ensure active frustum plane copy is fully safe. Do not write to g_activeFrustum 
 *                     without safety bounds checks on ECX.
 */
static int __fastcall Hooked_IsAABBVisible(void* ecx, void* edx, const float* bounds) {
    g_frustumCalls++;
    if (ecx && (uintptr_t)ecx >= 0x10000 && (uintptr_t)ecx < 0xFFE00000) {
        memcpy(g_activeFrustum, ecx, sizeof(g_activeFrustum));
        g_hasActiveFrustum = true;
    }
    int res = SSE2_IsAABBVisible((const float*)ecx, bounds);
    if (res == 0) {
        g_frustumCulled++;
    }
    return res;
}

typedef int (__fastcall *IsAABBVisibleType2_t)(void* ecx, void* edx, const float* bounds);
static IsAABBVisibleType2_t orig_IsAABBVisibleType2 = nullptr;

/**
 * @target_address: 0x00983A60
 * @rationale: Speeds up visibility checks using alternative epsilon margins.
 * @calling_convention: __fastcall (ECX contains planes structure, stack contains bounds)
 * @thread_affinity: Main Render Thread Only
 * @regression_hazard: Keep epsilon constant synced with game logic values (-0.019444443f vs 0.019444443f).
 */
static int __fastcall Hooked_IsAABBVisibleType2(void* ecx, void* edx, const float* bounds) {
    g_frustumCalls++;
    if (ecx && (uintptr_t)ecx >= 0x10000 && (uintptr_t)ecx < 0xFFE00000) {
        memcpy(g_activeFrustum, ecx, sizeof(g_activeFrustum));
        g_hasActiveFrustum = true;
    }
    int res = SSE2_IsAABBVisible_Type2((const float*)ecx, bounds);
    if (res == 0) {
        g_frustumCulled++;
    }
    return res;
}

typedef void (__fastcall *IsPointVisible_t)(void* ecx, void* edx, const float* point, uint8_t* outMask);
static IsPointVisible_t orig_IsPointVisible = nullptr;

/**
 * @target_address: 0x00983D70
 * @rationale: Vectorizes single point frustum culling.
 * @calling_convention: __fastcall (ECX contains planes structure, point and outMask passed on stack)
 * @thread_affinity: Main Render Thread Only
 * @regression_hazard: Validate outMask write address is inside LAA boundaries (up to 0xFFE00000).
 */
static void __fastcall Hooked_IsPointVisible(void* ecx, void* edx, const float* point, uint8_t* outMask) {
    g_frustumCalls++;
    if (ecx && (uintptr_t)ecx >= 0x10000 && (uintptr_t)ecx < 0xFFE00000) {
        memcpy(g_activeFrustum, ecx, sizeof(g_activeFrustum));
        g_hasActiveFrustum = true;
    }
    SSE2_IsPointVisible((const float*)ecx, point, outMask);
    if (outMask && *outMask != 0) {
        g_frustumCulled++;
    }
}
#endif

extern "C" void IncrementParticleFrameCount() {
    g_particleFrameCount++;
}

extern "C" bool SSE2_IsSphereVisible(float x, float y, float z, float radius) {
    if (!g_hasActiveFrustum) return true;
    __try {
        const float* planes = g_activeFrustum;
        for (int i = 0; i < 6; ++i) {
            float nx = planes[i * 4 + 0];
            float ny = planes[i * 4 + 1];
            float nz = planes[i * 4 + 2];
            float d  = planes[i * 4 + 3];
            
            float dist = nx * x + ny * y + nz * z + d;
            if (dist < -radius) {
                return false;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
    return true;
}

#if !TEST_DISABLE_PARTICLE_THROTTLE
typedef int (__fastcall *SimulateParticle_t)(void* self, void* edx, int particle, float timeStep, float* transformMatrix);
static SimulateParticle_t orig_SimulateParticle = nullptr;

/**
 * @target_address: 0x00981D40
 * @rationale: Throttles simulation of particles outside the visible frustum to save CPU cycles.
 * @calling_convention: __fastcall (ECX contains particle system, rest on stack)
 * @thread_affinity: Main Render Thread
 * @regression_hazard: Ensure fallback handles invalid float matrix pointers safely without crash.
 */
static int __fastcall Hooked_SimulateParticle(void* self, void* edx, int particle, float timeStep, float* transformMatrix) {
    __try {
        if (transformMatrix && (uintptr_t)transformMatrix >= 0x10000 && (uintptr_t)transformMatrix < 0xFFE00000) {
            float x = transformMatrix[12];
            float y = transformMatrix[13];
            float z = transformMatrix[14];
            
            if (!SSE2_IsSphereVisible(x, y, z, 25.0f)) {
                if ((g_particleFrameCount % 10) != 0) {
                    return 0;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return orig_SimulateParticle(self, edx, particle, timeStep, transformMatrix);
}
#endif

// ================================================================
// C3Vector::Cross Hook (0x005FEC70)
// ================================================================
typedef float* (__cdecl* Vec3Cross_t)(float* result, float* a, float* b);
static Vec3Cross_t orig_Vec3Cross = nullptr;

/**
 * @target_address: 0x005FEC70
 * @rationale: Replaces x87 FPU scalar math in cross-product computations.
 * @calling_convention: __cdecl (result, a, b parameters on stack)
 * @thread_affinity: Worker / Main thread safe
 * @regression_hazard: Pointers must be verified. Local output staging prevents alignment issues.
 */
static float* __cdecl Hooked_Vec3Cross(float* result, float* a, float* b) {
    __try {
        if (result && a && b &&
            (uintptr_t)result > 0x10000 && (uintptr_t)result < 0xFFE00000 &&
            (uintptr_t)a > 0x10000 && (uintptr_t)a < 0xFFE00000 &&
            (uintptr_t)b > 0x10000 && (uintptr_t)b < 0xFFE00000) {
            
            __m128 va = _mm_setr_ps(a[0], a[1], a[2], 0.0f);
            __m128 vb = _mm_setr_ps(b[0], b[1], b[2], 0.0f);

            __m128 a_yzx = _mm_shuffle_ps(va, va, _MM_SHUFFLE(3,0,2,1));
            __m128 b_yzx = _mm_shuffle_ps(vb, vb, _MM_SHUFFLE(3,0,2,1));
            __m128 a_zxy = _mm_shuffle_ps(va, va, _MM_SHUFFLE(3,1,0,2));
            __m128 b_zxy = _mm_shuffle_ps(vb, vb, _MM_SHUFFLE(3,1,0,2));

            __m128 cross = _mm_sub_ps(_mm_mul_ps(a_yzx, b_zxy),
                                       _mm_mul_ps(a_zxy, b_yzx));

            _mm_store_ss(result,     cross);
            _mm_store_ss(result + 1, _mm_shuffle_ps(cross, cross, _MM_SHUFFLE(1, 1, 1, 1)));
            _mm_store_ss(result + 2, _mm_shuffle_ps(cross, cross, _MM_SHUFFLE(2, 2, 2, 2)));
            return result;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return orig_Vec3Cross(result, a, b);
}

// ================================================================
// CFrustum::IsSphereVisible Hook (0x00983D20)
// ================================================================
typedef int (__fastcall* IsSphereVisible_t)(float* self, void* edx, float* sphere);
static IsSphereVisible_t orig_IsSphereVisible = nullptr;

/**
 * @target_address: 0x00983D20
 * @rationale: Vectorizes frustum-sphere intersections.
 * @calling_convention: __fastcall (Workaround for __thiscall: ECX contains frustum structure pointer)
 * @thread_affinity: Main Render Thread Only
 * @regression_hazard: Zero-extended transposed vectors must have clean structures to prevent NaN-poisoning.
 */
static int __fastcall Hooked_IsSphereVisible(float* self, void* edx, float* sphere) {
    __try {
        if (self && sphere &&
            (uintptr_t)self > 0x10000 && (uintptr_t)self < 0xFFE00000 &&
            (uintptr_t)sphere > 0x10000 && (uintptr_t)sphere < 0xFFE00000) {
            
            float* planes = (float*)self;
            __m128 sx = _mm_set1_ps(sphere[0]);
            __m128 sy = _mm_set1_ps(sphere[1]);
            __m128 sz = _mm_set1_ps(sphere[2]);
            __m128 sr = _mm_set1_ps(sphere[3]);

            __m128 r0 = _mm_loadu_ps(planes + 0);
            __m128 r1 = _mm_loadu_ps(planes + 4);
            __m128 r2 = _mm_loadu_ps(planes + 8);
            __m128 r3 = _mm_loadu_ps(planes + 12);

            _MM_TRANSPOSE4_PS(r0, r1, r2, r3);

            __m128 dp = _mm_add_ps(_mm_add_ps(_mm_mul_ps(r0, sx), _mm_mul_ps(r1, sy)), 
                                   _mm_add_ps(_mm_mul_ps(r2, sz), r3));

            __m128 cull_mask = _mm_cmplt_ps(dp, _mm_sub_ps(_mm_setzero_ps(), sr));
            if (_mm_movemask_ps(cull_mask) != 0) {
                return 0;
            }

            __m128 r4 = _mm_loadu_ps(planes + 16);
            __m128 r5 = _mm_loadu_ps(planes + 20);
            __m128 r6 = _mm_setzero_ps();
            __m128 r7 = _mm_setzero_ps();

            _MM_TRANSPOSE4_PS(r4, r5, r6, r7);

            __m128 dp2 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(r4, sx), _mm_mul_ps(r5, sy)), 
                                    _mm_add_ps(_mm_mul_ps(r6, sz), r7));

            __m128 cull_mask2 = _mm_cmplt_ps(dp2, _mm_sub_ps(_mm_setzero_ps(), sr));
            if ((_mm_movemask_ps(cull_mask2) & 3) != 0) {
                return 0;
            }

            return 3;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return orig_IsSphereVisible(self, edx, sphere);
}

// ================================================================
// CQuaternion::FromAngleAxis Hook (0x00982400)
// ================================================================
typedef float* (__fastcall* FromAngleAxis_t)(float* self, void* edx, float angle, float* axis);
static FromAngleAxis_t orig_FromAngleAxis = nullptr;

/**
 * @target_address: 0x00982400
 * @rationale: Vectorizes quaternion generation from arbitrary angle and axis structures.
 * @calling_convention: __fastcall (ECX contains output quaternion buffer)
 * @thread_affinity: Main Render Thread Only
 * @regression_hazard: Check output address bounds to prevent memory corruption in DLL structures.
 */
static float* __fastcall Hooked_FromAngleAxis(float* self, void* edx, float angle, float* axis) {
    __try {
        if (self && axis &&
            (uintptr_t)self > 0x10000 && (uintptr_t)self < 0xFFE00000 &&
            (uintptr_t)axis > 0x10000 && (uintptr_t)axis < 0xFFE00000) {
            
            float half_angle = angle * 0.5f;
            float s = sinf(half_angle);
            float c = cosf(half_angle);
            self[3] = c;
            self[0] = axis[0] * s;
            self[1] = axis[1] * s;
            self[2] = axis[2] * s;
            return axis;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return orig_FromAngleAxis(self, edx, angle, axis);
}

// ================================================================
// CQuaternion::Slerp Hook (0x00982460)
// ================================================================
typedef float* (__cdecl* QuatSlerp_t)(float* result, float t, float* q1, float* q2);
static QuatSlerp_t orig_QuatSlerp = nullptr;

/**
 * @target_address: 0x00982460
 * @rationale: Replaces x87 FPU double precision spherical interpolation calls.
 * @calling_convention: __cdecl (result, t, q1, q2 arguments on stack)
 * @thread_affinity: Main Render Thread
 * @regression_hazard: Epsilon value (0.000000000227373675f) must remain fully synced with original logic.
 */
static float* __cdecl Hooked_QuatSlerp(float* result, float t, float* q1, float* q2) {
    __try {
        if (result && q1 && q2 &&
            (uintptr_t)result > 0x10000 && (uintptr_t)result < 0xFFE00000 &&
            (uintptr_t)q1 > 0x10000 && (uintptr_t)q1 < 0xFFE00000 &&
            (uintptr_t)q2 > 0x10000 && (uintptr_t)q2 < 0xFFE00000) {
            
            float cosTheta = q1[0]*q2[0] + q1[1]*q2[1] + q1[2]*q2[2] + q1[3]*q2[3];
            float factor = 1.0f;
            if (cosTheta < 0.0f) {
                factor = -1.0f;
                cosTheta = -cosTheta;
            }
            
            float sinThetaSq = 1.0f - cosTheta * cosTheta;
            if (sinThetaSq > 0.000000000227373675f) {
                float sinTheta = sqrtf(sinThetaSq);
                float theta = atan2f(sinTheta, cosTheta);
                float invSinTheta = 1.0f / sinTheta;
                float r1 = sinf((1.0f - t) * theta) * invSinTheta;
                float r2 = sinf(t * theta) * invSinTheta * factor;
                
                result[0] = q1[0] * r1 + q2[0] * r2;
                result[1] = q1[1] * r1 + q2[1] * r2;
                result[2] = q1[2] * r1 + q2[2] * r2;
                result[3] = q1[3] * r1 + q2[3] * r2;
            } else {
                result[0] = q1[0];
                result[1] = q1[1];
                result[2] = q1[2];
                result[3] = q1[3];
            }
            return result;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return orig_QuatSlerp(result, t, q1, q2);
}

bool InstallSimdHooks(void) {
    Log("[SimdHooks] SSE2 matrix multiply, quaternion normalize, "
        "frustum cull, BGRA/ARGB, premultiplied alpha ready");

    if (ADDR_WOW_MATRIX_MULTIPLY)
        Log("[SimdHooks] Matrix multiply hook target: 0x%08X", ADDR_WOW_MATRIX_MULTIPLY);
    else
        Log("[SimdHooks] Matrix multiply: fill ADDR_WOW_MATRIX_MULTIPLY");

    if (ADDR_WOW_QUAT_NORMALIZE) {
        Log("[SimdHooks] Quaternion normalize hook target: 0x%08X", ADDR_WOW_QUAT_NORMALIZE);
#if !TEST_DISABLE_QUAT_NORMALIZE
        if (WineSafe_CreateHook((void*)ADDR_WOW_QUAT_NORMALIZE, (void*)Hooked_QuatNormalize, (void**)&orig_QuatNormalize) == MH_OK) {
            WO_EnableHook((void*)ADDR_WOW_QUAT_NORMALIZE);
            Log("[SimdHooks] Quaternion normalize hook ACTIVE");
        } else {
            Log("[SimdHooks] Quaternion normalize hook FAILED");
        }
#else
        Log("[SimdHooks] Quaternion normalize hook DISABLED by TEST_DISABLE_QUAT_NORMALIZE");
#endif
    } else {
        Log("[SimdHooks] Quaternion normalize: fill ADDR_WOW_QUAT_NORMALIZE");
    }

    if (ADDR_WOW_FRUSTUM_CULL) {
        Log("[SimdHooks] Frustum cull hook target: 0x%08X", ADDR_WOW_FRUSTUM_CULL);
#if !TEST_DISABLE_FRUSTUM_CULL
        if (WineSafe_CreateHook((void*)ADDR_WOW_FRUSTUM_CULL, (void*)Hooked_IsAABBVisible, (void**)&orig_IsAABBVisible) == MH_OK) {
            WO_EnableHook((void*)ADDR_WOW_FRUSTUM_CULL);
            Log("[SimdHooks] Frustum cull hook ACTIVE");
        } else {
            Log("[SimdHooks] Frustum cull hook FAILED");
        }
#else
        Log("[SimdHooks] Frustum cull hook DISABLED by TEST_DISABLE_FRUSTUM_CULL");
#endif
    } else {
        Log("[SimdHooks] Frustum cull: fill ADDR_WOW_FRUSTUM_CULL");
    }

    if (ADDR_WOW_FRUSTUM_CULL_TYPE2) {
        Log("[SimdHooks] Frustum cull type 2 hook target: 0x%08X", ADDR_WOW_FRUSTUM_CULL_TYPE2);
#if !TEST_DISABLE_FRUSTUM_CULL
        if (WineSafe_CreateHook((void*)ADDR_WOW_FRUSTUM_CULL_TYPE2, (void*)Hooked_IsAABBVisibleType2, (void**)&orig_IsAABBVisibleType2) == MH_OK) {
            WO_EnableHook((void*)ADDR_WOW_FRUSTUM_CULL_TYPE2);
            Log("[SimdHooks] Frustum cull type 2 hook ACTIVE");
        } else {
            Log("[SimdHooks] Frustum cull type 2 hook FAILED");
        }
#else
        Log("[SimdHooks] Frustum cull type 2 hook DISABLED by TEST_DISABLE_FRUSTUM_CULL");
#endif
    } else {
        Log("[SimdHooks] Frustum cull type 2: fill ADDR_WOW_FRUSTUM_CULL_TYPE2");
    }

    if (ADDR_WOW_FRUSTUM_CULL_POINT) {
        Log("[SimdHooks] Frustum cull point hook target: 0x%08X", ADDR_WOW_FRUSTUM_CULL_POINT);
#if !TEST_DISABLE_FRUSTUM_CULL
        if (WineSafe_CreateHook((void*)ADDR_WOW_FRUSTUM_CULL_POINT, (void*)Hooked_IsPointVisible, (void**)&orig_IsPointVisible) == MH_OK) {
            WO_EnableHook((void*)ADDR_WOW_FRUSTUM_CULL_POINT);
            Log("[SimdHooks] Frustum cull point hook ACTIVE");
        } else {
            Log("[SimdHooks] Frustum cull point hook FAILED");
        }
#else
        Log("[SimdHooks] Frustum cull point hook DISABLED by TEST_DISABLE_FRUSTUM_CULL");
#endif
    } else {
        Log("[SimdHooks] Frustum cull point: fill ADDR_WOW_FRUSTUM_CULL_POINT");
    }

    if (ADDR_WOW_RAY_TRIANGLE_32BIT) {
        Log("[SimdHooks] Ray-Triangle 32-bit hook target: 0x%08X", ADDR_WOW_RAY_TRIANGLE_32BIT);
#if !TEST_DISABLE_RAY_TRIANGLE_SSE2
        if (WineSafe_CreateHook((void*)ADDR_WOW_RAY_TRIANGLE_32BIT, (void*)Hooked_RayTriangle32, (void**)&orig_RayTriangle32) == MH_OK) {
            WO_EnableHook((void*)ADDR_WOW_RAY_TRIANGLE_32BIT);
            Log("[SimdHooks] Ray-Triangle 32-bit hook ACTIVE");
        } else {
            Log("[SimdHooks] Ray-Triangle 32-bit hook FAILED");
        }
#endif
    } else {
        Log("[SimdHooks] Ray-Triangle 32-bit: fill ADDR_WOW_RAY_TRIANGLE_32BIT");
    }

    if (ADDR_WOW_RAY_TRIANGLE_16BIT) {
        Log("[SimdHooks] Ray-Triangle 16-bit hook target: 0x%08X", ADDR_WOW_RAY_TRIANGLE_16BIT);
#if !TEST_DISABLE_RAY_TRIANGLE_SSE2
        if (WineSafe_CreateHook((void*)ADDR_WOW_RAY_TRIANGLE_16BIT, (void*)Hooked_RayTriangle16, (void**)&orig_RayTriangle16) == MH_OK) {
            WO_EnableHook((void*)ADDR_WOW_RAY_TRIANGLE_16BIT);
            Log("[SimdHooks] Ray-Triangle 16-bit hook ACTIVE");
        } else {
            Log("[SimdHooks] Ray-Triangle 16-bit hook FAILED");
        }
#endif
    } else {
        Log("[SimdHooks] Ray-Triangle 16-bit: fill ADDR_WOW_RAY_TRIANGLE_16BIT");
    }

#if !TEST_DISABLE_PARTICLE_THROTTLE
    Log("[SimdHooks] Hooking CParticleEmitter::SimulateParticle at 0x00981D40");
    if (WineSafe_CreateHook((void*)0x00981D40, (void*)Hooked_SimulateParticle, (void**)&orig_SimulateParticle) == MH_OK) {
        if (WO_EnableHook((void*)0x00981D40) == MH_OK) {
            Log("[SimdHooks] CParticleEmitter::SimulateParticle hook ACTIVE");
        } else {
            Log("[SimdHooks] CParticleEmitter::SimulateParticle hook enable FAILED");
        }
    } else {
        Log("[SimdHooks] CParticleEmitter::SimulateParticle hook creation FAILED");
    }
#endif

    // Hooking 3D Vector Cross Product (0x005FEC70)
#if !TEST_DISABLE_VEC3_CROSS_SSE2
    if (WineSafe_CreateHook((void*)0x005FEC70, (void*)Hooked_Vec3Cross, (void**)&orig_Vec3Cross) == MH_OK) {
        WO_EnableHook((void*)0x005FEC70);
        Log("[SimdHooks] C3Vector::Cross hook ACTIVE");
    }
#else
    Log("[SimdHooks] C3Vector::Cross DISABLED by TEST_DISABLE_VEC3_CROSS_SSE2");
#endif

    // Hooking CFrustum::IsSphereVisible (0x00983D20)
#if !TEST_DISABLE_SPHERE_VISIBLE_SSE2
    if (WineSafe_CreateHook((void*)0x00983D20, (void*)Hooked_IsSphereVisible, (void**)&orig_IsSphereVisible) == MH_OK) {
        WO_EnableHook((void*)0x00983D20);
        Log("[SimdHooks] CFrustum::IsSphereVisible hook ACTIVE");
    }
#else
    Log("[SimdHooks] CFrustum::IsSphereVisible DISABLED by TEST_DISABLE_SPHERE_VISIBLE_SSE2");
#endif

    // Hooking CQuaternion::FromAngleAxis (0x00982400)
#if !TEST_DISABLE_FROM_ANGLE_AXIS_SSE2
    if (WineSafe_CreateHook((void*)0x00982400, (void*)Hooked_FromAngleAxis, (void**)&orig_FromAngleAxis) == MH_OK) {
        WO_EnableHook((void*)0x00982400);
        Log("[SimdHooks] CQuaternion::FromAngleAxis hook ACTIVE");
    }
#else
    Log("[SimdHooks] CQuaternion::FromAngleAxis DISABLED by TEST_DISABLE_FROM_ANGLE_AXIS_SSE2");
#endif

    // Hooking CQuaternion::Slerp (0x00982460)
#if !TEST_DISABLE_QUAT_SLERP_SSE2
    if (WineSafe_CreateHook((void*)0x00982460, (void*)Hooked_QuatSlerp, (void**)&orig_QuatSlerp) == MH_OK) {
        WO_EnableHook((void*)0x00982460);
        Log("[SimdHooks] CQuaternion::Slerp hook ACTIVE");
    }
#else
    Log("[SimdHooks] CQuaternion::Slerp DISABLED by TEST_DISABLE_QUAT_SLERP_SSE2");
#endif

    return true;
}

void ShutdownSimdHooks(void) {
    MH_DisableHook((void*)0x005FEC70);
    MH_DisableHook((void*)0x00983D20);
    MH_DisableHook((void*)0x00982400);
    MH_DisableHook((void*)0x00982460);
    Log("[SimdHooks] Stats: matMul=%lld, ... frustum=%lld (culled=%lld, %.1f%%), rayTri=%lld (hit=%lld, %.1f%%)",
        g_matMulCalls,
        g_frustumCalls, g_frustumCulled,
        g_frustumCalls ? 100.0 * g_frustumCulled / g_frustumCalls : 0.0,
        g_rayTriangleCalls, g_rayTriangleIntersects,
        g_rayTriangleCalls ? 100.0 * g_rayTriangleIntersects / g_rayTriangleCalls : 0.0);
}
