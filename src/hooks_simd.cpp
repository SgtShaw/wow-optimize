// ================================================================
// hooks_simd.cpp — SIMD Micro-Architectural Acceleration
// ================================================================
// SSE2/SSE4.1 vectorized replacements for WoW's legacy x87 math:
//   1. Frustum culling — SSE2 plane vs AABB (4 planes at once)
//   2. Color/alpha batch — SSE2 BGRA/RGBA with premultiplied alpha
//   3. SSE2 4x4 matrix multiply — replaces CMatrix::operator*
//   4. SSE2 quaternion normalize — replaces CQuaternion::Normalize
//
// All functions are SSE2-only (32-bit x86 has guaranteed SSE2 on
// CPUs that run WoW 3.3.5a). AVX is not used (not available on x86).
// ================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <xmmintrin.h>  // SSE
#include <emmintrin.h>  // SSE2
#include <tmmintrin.h>  // SSSE3 (_mm_shuffle_epi8 for color swizzle)
#include "MinHook.h"
#include "version.h"
#include "hooks_simd.h"

extern "C" void Log(const char* fmt, ...);

// ================================================================
// SIMD Utility: 4x4 Matrix Multiply (SSE2)
// ================================================================
// Replaces WoW's CMatrix::operator* which uses x87 FPU scalar ops.
// Input: two 4x4 column-major float matrices (64 bytes each).
// Output: result matrix (64 bytes).
//
// WoW matrix layout (CM44Matrix / C4Matrix):
//   m[0]  m[1]  m[2]  m[3]   right vector (columns 0-3)
//   m[4]  m[5]  m[6]  m[7]   up vector
//   m[8]  m[9]  m[10] m[11]  forward vector
//   m[12] m[13] m[14] m[15]  position

void SSE2_MatrixMultiply(const float* __restrict a,
                         const float* __restrict b,
                         float* __restrict result) {
    // Load all columns of b (4 columns x 4 floats = 4 SSE vectors)
    __m128 b0 = _mm_loadu_ps(b);       // b[0..3]
    __m128 b1 = _mm_loadu_ps(b + 4);   // b[4..7]
    __m128 b2 = _mm_loadu_ps(b + 8);   // b[8..11]
    __m128 b3 = _mm_loadu_ps(b + 12);  // b[12..15]

    // Multiply a rows by b columns
    for (int row = 0; row < 4; row++) {
        __m128 a_row = _mm_loadu_ps(a + row * 4); // a[row*4 .. row*4+3]

        // broadcast each element of a_row and multiply by b columns
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
// Replaces WoW's CQuaternion::Normalize (often called per-bone
// in animation blends, 50+ bones per model x 20+ models per frame).
// Uses SSE2 rsqrt approximation for ~3x throughput vs x87 sqrt.

void SSE2_QuatNormalize(float* __restrict q) {
    __m128 v = _mm_loadu_ps(q); // x, y, z, w
    __m128 dot = _mm_mul_ps(v, v);

    // Horizontal sum broadcast to ALL lanes. SSE2 has no _mm_hadd_ps, so do a
    // two-step shuffle+add. Both steps must fold across the full register or some
    // lanes hold a partial sum (the earlier _MM_SHUFFLE(1,1,1,1) form left lanes
    // 0/1 at 2*(x^2+y^2), mis-scaling x and y).
    __m128 shuf = _mm_shuffle_ps(dot, dot, _MM_SHUFFLE(2,3,0,1));   // [y2, x2, w2, z2]
    __m128 sum1 = _mm_add_ps(dot, shuf);                            // [x2+y2, x2+y2, z2+w2, z2+w2]
    __m128 shuf2 = _mm_shuffle_ps(sum1, sum1, _MM_SHUFFLE(1,0,3,2));// [z2+w2, z2+w2, x2+y2, x2+y2]
    __m128 sum2 = _mm_add_ps(sum1, shuf2);                          // full sum in every lane

    // Match the engine's guard: a near-zero magnitude quaternion is left
    // unchanged (the original sub_979110 only normalizes when mag^2 > 2^-22).
    // Without this, rsqrt(0)=Inf and the Newton step yields NaN, poisoning the
    // bone/transform pipeline.
    float mag2;
    _mm_store_ss(&mag2, sum2);
    if (mag2 <= 0.00000023841858f) {
        return;
    }

    // rsqrt approximation + one Newton-Raphson iteration (~23-bit accuracy)
    __m128 rlen = _mm_rsqrt_ps(sum2);
    __m128 half = _mm_set1_ps(0.5f);
    __m128 three = _mm_set1_ps(3.0f);
    __m128 rlen2 = _mm_mul_ps(sum2, _mm_mul_ps(rlen, rlen));
    rlen = _mm_mul_ps(_mm_mul_ps(half, rlen), _mm_sub_ps(three, rlen2));

    // Multiply quaternion components by reciprocal length
    v = _mm_mul_ps(v, rlen);
    _mm_storeu_ps(q, v);
}

// ================================================================
// SIMD Utility: Vector3 Normalize (SSE2)
// ================================================================
void SSE2_Vec3Normalize(float* __restrict v) {
    __m128 xyz = _mm_loadu_ps(v);   // x, y, z, ? (fourth component loaded but ignored)
    __m128 sq  = _mm_mul_ps(xyz, xyz);

    __m128 shuf = _mm_shuffle_ps(sq, sq, _MM_SHUFFLE(2,3,0,1));
    __m128 sum1 = _mm_add_ps(sq, shuf);
    __m128 sum2 = _mm_add_ss(sum1, _mm_shuffle_ps(sum1, sum1, _MM_SHUFFLE(1,1,1,1)));

    __m128 rlen = _mm_rsqrt_ss(sum2);

    // NR iteration
    __m128 half = _mm_set_ss(0.5f);
    __m128 three = _mm_set_ss(3.0f);
    __m128 rlen2 = _mm_mul_ss(sum2, _mm_mul_ss(rlen, rlen));
    rlen = _mm_mul_ss(_mm_mul_ss(half, rlen), _mm_sub_ss(three, rlen2));

    rlen = _mm_shuffle_ps(rlen, rlen, _MM_SHUFFLE(0,0,0,0));
    xyz = _mm_mul_ps(xyz, rlen);
    _mm_storeu_ps(v, xyz);
}

// ================================================================
// 5. Frustum Culling SIMD Acceleration
// ================================================================
// WoW's frustum culling tests each object's AABB against 6 planes
// one plane at a time using scalar x87 compares. We replace the
// inner loop with SSE2 to test 4 planes simultaneously:
//   - Load object's AABB min/max into SSE registers
//   - For each plane, compute signed distance = dot(normal, point) + d
//   - Process 4 planes at once using parallel SSE dot products
//   - Early-out when all 8 corners are outside any single plane
//
// Plane equation: n·p + d = 0, where n=(nx,ny,nz) is inward-facing.
// A point p is inside if n·p + d >= 0.
// An AABB is outside if all 8 corners have n·p + d < 0 for the same plane.

// Frustum plane (packed for SSE: nx, ny, nz, d)
struct SSEPlane {
    __m128 normal_d; // nx, ny, nz, d
};

// AABB: min point + max point
struct SSEAABB {
    __m128 min;  // minX, minY, minZ, 0
    __m128 max;  // maxX, maxY, maxZ, 0
};

// Test one AABB against 4 frustum planes simultaneously.
// Returns 0 if the AABB is culled (outside any plane),
// returns non-zero if potentially visible.
static int SSE2_FrustumCull4(const SSEAABB* aabb, const SSEPlane* planes) {
    // For each plane, compute the min and max dot product across
    // all 8 AABB corners by selecting min/max components based
    // on the sign of the plane normal.

    // Load AABB
    __m128 bbMin = aabb->min;
    __m128 bbMax = aabb->max;

    // Test all 4 planes
    for (int p = 0; p < 4; p++) {
        __m128 plane = planes[p].normal_d;

        // Select AABB corner closest to plane (p-vertex) and
        // farthest from plane (n-vertex) based on normal sign.
        // p-vertex: max where normal>=0, min where normal<0
        // n-vertex: min where normal>=0, max where normal<0

        __m128 normal = plane; // nx, ny, nz, d (we only use xyz for selection)

        // Mask: 0xFFFFFFFF where normal < 0, 0x00000000 where normal >= 0
        __m128 negMask = _mm_cmplt_ps(normal, _mm_setzero_ps());

        // n-vertex (farthest): pick max where normal<0, min where normal>=0
        __m128 nVertexMin = _mm_or_ps(_mm_and_ps(negMask, bbMax), _mm_andnot_ps(negMask, bbMin));
        __m128 nVertexMax = _mm_or_ps(_mm_andnot_ps(negMask, bbMax), _mm_and_ps(negMask, bbMin));

        // Wait — n-vertex is the farthest along the plane normal direction.
        // If normal_i >= 0: n-vertex uses max_i (farthest positive direction)
        // If normal_i < 0:  n-vertex uses min_i (farthest negative direction)
        // So n-vertex = (normal >= 0) ? max : min
        __m128 posMask = _mm_cmpge_ps(normal, _mm_setzero_ps());
        __m128 nVertex = _mm_or_ps(_mm_and_ps(posMask, bbMax), _mm_andnot_ps(posMask, bbMin));

        // p-vertex is the opposite: (normal >= 0) ? min : max
        __m128 pVertex = _mm_or_ps(_mm_and_ps(posMask, bbMin), _mm_andnot_ps(posMask, bbMax));

        // Dot product with plane: nVertex·normal + d
        // If nVertex·normal + d < 0, all 8 corners are outside → cull
        __m128 dotN = _mm_mul_ps(nVertex, plane);
        // Horizontal sum of xyz components + add d
        __m128 shuf1 = _mm_shuffle_ps(dotN, dotN, _MM_SHUFFLE(2,3,0,1));
        __m128 sum1  = _mm_add_ps(dotN, shuf1);
        __m128 sum2  = _mm_add_ss(sum1, _mm_shuffle_ps(sum1, sum1, _MM_SHUFFLE(1,1,1,1)));
        // sum2 now has (nx*nVertex_x + ny*nVertex_y + nz*nVertex_z) in lowest float
        // Add d (plane.w)
        sum2 = _mm_add_ss(sum2, _mm_shuffle_ps(plane, plane, _MM_SHUFFLE(3,3,3,3)));

        float dist;
        _mm_store_ss(&dist, sum2);

        if (dist < 0.0f) {
            return 0; // AABB is completely outside this plane → culled
        }
    }

    return 1; // potentially visible
}

// Test AABB against 6 frustum planes (2 SSE passes: 4+2)
int SSE2_FrustumCull6(const float aabbMin[3], const float aabbMax[3],
                      const float frustumPlanes[6][4]) {
    SSEAABB aabb;
    aabb.min = _mm_setr_ps(aabbMin[0], aabbMin[1], aabbMin[2], 0.0f);
    aabb.max = _mm_setr_ps(aabbMax[0], aabbMax[1], aabbMax[2], 0.0f);

    // Test first 4 planes
    SSEPlane planes4[4];
    for (int i = 0; i < 4; i++) {
        planes4[i].normal_d = _mm_loadu_ps(frustumPlanes[i]);
    }
    if (!SSE2_FrustumCull4(&aabb, planes4)) return 0;

    // Test remaining 2 planes (reuse SSE2_FrustumCull4 with 2 dummy planes
    // that always pass)
    SSEPlane planes2[4];
    for (int i = 0; i < 2; i++) {
        planes2[i].normal_d = _mm_loadu_ps(frustumPlanes[4 + i]);
    }
    // Dummy planes: normal = (0,0,1), d = large positive → never culls
    planes2[2].normal_d = _mm_setr_ps(0.0f, 0.0f, 1.0f, 1e10f);
    planes2[3].normal_d = _mm_setr_ps(0.0f, 0.0f, 1.0f, 1e10f);

    return SSE2_FrustumCull4(&aabb, planes2);
}

// ================================================================
// 6. Color/Alpha Batch SIMD Conversion
// ================================================================
// WoW uses ARGB format internally but D3D9 sometimes expects
// different layouts per texture format. These functions batch-
// convert pixel data using SSE2.
//
// Common conversions needed:
//   BGRA → ARGB (swap R and B channels)
//   ARGB → premultiplied alpha

// Convert 16 pixels from BGRA → ARGB using SSE2
// Processes 4 pixels per SSE register:
//   Input:  B0 G0 R0 A0 | B1 G1 R1 A1 | B2 G2 R2 A2 | B3 G3 R3 A3
//   Output: R0 G0 B0 A0 | R1 G1 B1 A1 | R2 G2 B2 A2 | R3 G3 B3 A3
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

    // Tail: process remaining pixels (0-3) with scalar fallback
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

// Batch premultiplied alpha: ARGB → A*R, A*G, A*B, A
// Processes 4 pixels at a time using SSE2 integer multiply.
void SSE2_PremultiplyAlpha_Batch(const uint8_t* __restrict src,
                                  uint8_t* __restrict dst,
                                  size_t pixelCount) {
    const __m128i alphaMask = _mm_setr_epi8(
        3,3,3,3,  7,7,7,7,  11,11,11,11,  15,15,15,15);
    const __m128i half = _mm_set1_epi16(128);
    const __m128i ones = _mm_set1_epi16(1);
    const __m128i zero = _mm_setzero_si128();

    size_t vecCount = pixelCount / 4;
    for (size_t i = 0; i < vecCount; i++) {
        __m128i pixel4 = _mm_loadu_si128((const __m128i*)(src + i * 16));

        // Extract alpha values and replicate to all bytes
        __m128i alpha = _mm_shuffle_epi8(pixel4, alphaMask);

        // Zero-extend to 16-bit for multiply
        __m128i pxLo = _mm_unpacklo_epi8(pixel4, zero);  // p0, p1
        __m128i pxHi = _mm_unpackhi_epi8(pixel4, zero);  // p2, p3
        __m128i alLo = _mm_unpacklo_epi8(alpha, zero);
        __m128i alHi = _mm_unpackhi_epi8(alpha, zero);

        // Multiply: result = (color * alpha + 128) / 255
        // Approximate with (color * alpha + 128) >> 8 + rounding
        pxLo = _mm_mullo_epi16(pxLo, alLo);
        pxHi = _mm_mullo_epi16(pxHi, alHi);

        // Divide by 255: (x + ((x + 128) >> 8)) >> 8
        // Approximation: (x * 257 + 32768) >> 16
        // Simpler: (x + 128) / 256 (slight error but fast)
        pxLo = _mm_srli_epi16(_mm_add_epi16(pxLo, half), 8);
        pxHi = _mm_srli_epi16(_mm_add_epi16(pxHi, half), 8);

        // Pack back to 8-bit
        pixel4 = _mm_packus_epi16(pxLo, pxHi);

        // Restore alpha channel (alpha = original alpha)
        // Bitwise: keep alpha from original, use new R,G,B from premultiplied
        const __m128i rgbMask = _mm_setr_epi8(
            (char)0xFF,(char)0xFF,(char)0xFF,0, (char)0xFF,(char)0xFF,(char)0xFF,0,
            (char)0xFF,(char)0xFF,(char)0xFF,0, (char)0xFF,(char)0xFF,(char)0xFF,0);
        pixel4 = _mm_or_si128(_mm_and_si128(pixel4, rgbMask),
                               _mm_andnot_si128(rgbMask, _mm_loadu_si128((const __m128i*)(src + i * 16))));

        _mm_storeu_si128((__m128i*)(dst + i * 16), pixel4);
    }

    // Tail scalar fallback
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

// ================================================================
// SSE2 Vector Cross Product
// ================================================================
void SSE2_Vec3Cross(const float* __restrict a,
                    const float* __restrict b,
                    float* __restrict result) {
    __m128 va = _mm_loadu_ps(a);  // ax, ay, az, ?
    __m128 vb = _mm_loadu_ps(b);  // bx, by, bz, ?

    // (a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x)
    __m128 a_yzx = _mm_shuffle_ps(va, va, _MM_SHUFFLE(3,0,2,1)); // ay, az, ax, ?
    __m128 b_yzx = _mm_shuffle_ps(vb, vb, _MM_SHUFFLE(3,0,2,1)); // by, bz, bx, ?
    __m128 a_zxy = _mm_shuffle_ps(va, va, _MM_SHUFFLE(3,1,0,2)); // az, ax, ay, ?
    __m128 b_zxy = _mm_shuffle_ps(vb, vb, _MM_SHUFFLE(3,1,0,2)); // bz, bx, by, ?

    __m128 cross = _mm_sub_ps(_mm_mul_ps(a_yzx, b_zxy),
                               _mm_mul_ps(a_zxy, b_yzx));

    _mm_storeu_ps(result, cross);
}

// ================================================================
// Hook infrastructure for SIMD replacements
// ================================================================
// These are pure function replacements — no state, no side effects.
// They can be hot-patched via the existing HotPatch infrastructure
// (hot_patch.cpp N21-N24) or called directly by hooked callers.

// To hook WoW's matrix multiply: find the address of CMatrix::operator*
// or the inline math routine that calls it. Use HotPatch to replace
// with our SSE2 version.
#ifndef ADDR_WOW_MATRIX_MULTIPLY
#define ADDR_WOW_MATRIX_MULTIPLY 0x00000000  // CMatrix::Multiply or operator*
#endif
#ifndef ADDR_WOW_QUAT_NORMALIZE
#define ADDR_WOW_QUAT_NORMALIZE 0x00979110  // CQuaternion::Normalize
#endif
#ifndef ADDR_WOW_FRUSTUM_CULL
#define ADDR_WOW_FRUSTUM_CULL  0x009839E0  // CFrustum::IsAABBVisible
#endif
#ifndef ADDR_WOW_FRUSTUM_CULL_TYPE2
#define ADDR_WOW_FRUSTUM_CULL_TYPE2 0x00983A60  // CFrustum::IsAABBVisible_Type2
#endif
#ifndef ADDR_WOW_FRUSTUM_CULL_POINT
#define ADDR_WOW_FRUSTUM_CULL_POINT 0x00983D70  // CFrustum::IsPointVisible
#endif
#ifndef ADDR_WOW_RAY_TRIANGLE_32BIT
#define ADDR_WOW_RAY_TRIANGLE_32BIT 0x009836B0  // sub_9836B0
#endif
#ifndef ADDR_WOW_RAY_TRIANGLE_16BIT
#define ADDR_WOW_RAY_TRIANGLE_16BIT 0x00983490  // sub_983490
#endif

// ================================================================
// Statistics
// ================================================================
static volatile LONG64 g_matMulCalls    = 0;
static volatile LONG64 g_quatNormCalls  = 0;
static volatile LONG64 g_frustumCalls   = 0;
static volatile LONG64 g_frustumCulled  = 0;
static volatile LONG64 g_rayTriangleCalls = 0;
static volatile LONG64 g_rayTriangleIntersects = 0;

// ================================================================
// Public API
// ================================================================

#include "version.h"
#include "MinHook.h"

// Test AABB against 6 planes in CFrustum
// bounds: float[6] (min_x, min_y, min_z, max_x, max_y, max_z)
// planes: float[6][4] at ecx
int SSE2_IsAABBVisible(const float* planes, const float* bounds) {
    __try {
        if (!planes || !bounds) return 3; // safe fallback (visible)
        
        // Validate pointers are in valid user-mode address space
        if ((uintptr_t)planes < 0x10000 || (uintptr_t)planes >= 0xBFFF0000) return 3;
        if ((uintptr_t)bounds < 0x10000 || (uintptr_t)bounds >= 0xBFFF0000) return 3;

        // Load bounds
        __m128 min_x = _mm_set1_ps(bounds[0]);
        __m128 min_y = _mm_set1_ps(bounds[1]);
        __m128 min_z = _mm_set1_ps(bounds[2]);
        __m128 max_x = _mm_set1_ps(bounds[3]);
        __m128 max_y = _mm_set1_ps(bounds[4]);
        __m128 max_z = _mm_set1_ps(bounds[5]);

        // Epsilon constant from WoW engine
        const __m128 eps = _mm_set1_ps(-0.019444443f);

        // Load planes 0 to 3
        __m128 r0 = _mm_loadu_ps(planes + 0);  // n0_x, n0_y, n0_z, d0
        __m128 r1 = _mm_loadu_ps(planes + 4);  // n1_x, n1_y, n1_z, d1
        __m128 r2 = _mm_loadu_ps(planes + 8);  // n2_x, n2_y, n2_z, d2
        __m128 r3 = _mm_loadu_ps(planes + 12); // n3_x, n3_y, n3_z, d3

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
            return 0; // Culled by at least one of planes 0..3
        }

        // Load planes 4 and 5, with dummy planes for 6 and 7
        __m128 r4 = _mm_loadu_ps(planes + 16); // n4_x, n4_y, n4_z, d4
        __m128 r5 = _mm_loadu_ps(planes + 20); // n5_x, n5_y, n5_z, d5
        // Dummy planes have normal = (0,0,0) and d = 1000.0f -> never culled
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
            return 0; // Culled by plane 4 or 5
        }

        return 3; // Visible
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 3; // safe fallback (visible)
    }
}

// Test AABB against 6 planes in CFrustum (Type 2: sum <= 0.019444443f)
// bounds: float[6] (min_x, min_y, min_z, max_x, max_y, max_z)
// planes: float[6][4] at ecx
int SSE2_IsAABBVisible_Type2(const float* planes, const float* bounds) {
    __try {
        if (!planes || !bounds) return 3; // safe fallback (visible)
        
        // Validate pointers are in valid user-mode address space
        if ((uintptr_t)planes < 0x10000 || (uintptr_t)planes >= 0xBFFF0000) return 3;
        if ((uintptr_t)bounds < 0x10000 || (uintptr_t)bounds >= 0xBFFF0000) return 3;

        // Load bounds
        __m128 min_x = _mm_set1_ps(bounds[0]);
        __m128 min_y = _mm_set1_ps(bounds[1]);
        __m128 min_z = _mm_set1_ps(bounds[2]);
        __m128 max_x = _mm_set1_ps(bounds[3]);
        __m128 max_y = _mm_set1_ps(bounds[4]);
        __m128 max_z = _mm_set1_ps(bounds[5]);

        // Epsilon constant from WoW engine
        const __m128 eps = _mm_set1_ps(0.019444443f);

        // Load planes 0 to 3
        __m128 r0 = _mm_loadu_ps(planes + 0);  // n0_x, n0_y, n0_z, d0
        __m128 r1 = _mm_loadu_ps(planes + 4);  // n1_x, n1_y, n1_z, d1
        __m128 r2 = _mm_loadu_ps(planes + 8);  // n2_x, n2_y, n2_z, d2
        __m128 r3 = _mm_loadu_ps(planes + 12); // n3_x, n3_y, n3_z, d3

        _MM_TRANSPOSE4_PS(r0, r1, r2, r3);

        __m128 mask_x = _mm_cmpge_ps(r0, _mm_setzero_ps());
        __m128 x_val = _mm_or_ps(_mm_and_ps(mask_x, max_x), _mm_andnot_ps(mask_x, min_x));

        __m128 mask_y = _mm_cmpge_ps(r1, _mm_setzero_ps());
        __m128 y_val = _mm_or_ps(_mm_and_ps(mask_y, max_y), _mm_andnot_ps(mask_y, min_y));

        __m128 mask_z = _mm_cmpge_ps(r2, _mm_setzero_ps());
        __m128 z_val = _mm_or_ps(_mm_and_ps(mask_z, max_z), _mm_andnot_ps(mask_z, min_z));

        __m128 dp = _mm_add_ps(_mm_add_ps(_mm_mul_ps(r0, x_val), _mm_mul_ps(r1, y_val)), 
                               _mm_add_ps(_mm_mul_ps(r2, z_val), r3));

        // Culled if dp > eps
        __m128 cull_mask = _mm_cmpgt_ps(dp, eps);
        if (_mm_movemask_ps(cull_mask) != 0) {
            return 0; // Culled by at least one of planes 0..3
        }

        // Load planes 4 and 5, with dummy planes for 6 and 7
        __m128 r4 = _mm_loadu_ps(planes + 16); // n4_x, n4_y, n4_z, d4
        __m128 r5 = _mm_loadu_ps(planes + 20); // n5_x, n5_y, n5_z, d5
        // Dummy planes have normal = (0,0,0) and d = -1000.0f -> never culled
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
            return 0; // Culled by plane 4 or 5
        }

        return 3; // Visible
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 3; // safe fallback (visible)
    }
}

// Test point against 6 planes in CFrustum and return culling mask
// point: float[3] (x, y, z)
// planes: float[6][4] at ecx
void SSE2_IsPointVisible(const float* planes, const float* point, uint8_t* outMask) {
    __try {
        if (!planes || !point || !outMask) return;
        
        // Validate pointers are in valid user-mode address space
        if ((uintptr_t)planes < 0x10000 || (uintptr_t)planes >= 0xBFFF0000) return;
        if ((uintptr_t)point < 0x10000 || (uintptr_t)point >= 0xBFFF0000) return;
        if ((uintptr_t)outMask < 0x10000 || (uintptr_t)outMask >= 0xBFFF0000) return;

        __m128 px = _mm_set1_ps(point[0]);
        __m128 py = _mm_set1_ps(point[1]);
        __m128 pz = _mm_set1_ps(point[2]);
        const __m128 eps = _mm_set1_ps(-0.019444443f);

        // Load planes 0 to 3
        __m128 r0 = _mm_loadu_ps(planes + 0);  // n0_x, n0_y, n0_z, d0
        __m128 r1 = _mm_loadu_ps(planes + 4);  // n1_x, n1_y, n1_z, d1
        __m128 r2 = _mm_loadu_ps(planes + 8);  // n2_x, n2_y, n2_z, d2
        __m128 r3 = _mm_loadu_ps(planes + 12); // n3_x, n3_y, n3_z, d3

        _MM_TRANSPOSE4_PS(r0, r1, r2, r3);

        __m128 dp = _mm_add_ps(_mm_add_ps(_mm_mul_ps(r0, px), _mm_mul_ps(r1, py)), 
                               _mm_add_ps(_mm_mul_ps(r2, pz), r3));

        __m128 mask = _mm_cmplt_ps(dp, eps);
        int bitmask1 = _mm_movemask_ps(mask);

        // Load planes 4 and 5, with dummy planes for 6 and 7
        __m128 r4 = _mm_loadu_ps(planes + 16); // n4_x, n4_y, n4_z, d4
        __m128 r5 = _mm_loadu_ps(planes + 20); // n5_x, n5_y, n5_z, d5
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
        // safe fallback: not culled
    }
}

// SSE2 3D cross product helper
// a = (ax, ay, az, 0)
// b = (bx, by, bz, 0)
// returns (ay*bz - az*by, az*bx - ax*bz, ax*by - ay*bx, 0)
static inline __m128 SSE2_Cross(const __m128& a, const __m128& b) {
    __m128 a_1 = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 0, 2, 1));
    __m128 b_1 = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 1, 0, 2));
    __m128 a_2 = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 1, 0, 2));
    __m128 b_2 = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 0, 2, 1));
    return _mm_sub_ps(_mm_mul_ps(a_1, b_1), _mm_mul_ps(a_2, b_2));
}

// SSE2 3D dot product helper
// a = (ax, ay, az, 0)
// b = (bx, by, bz, 0)
// returns (ax*bx + ay*by + az*bz) broadcasted to all lanes
static inline __m128 SSE2_Dot3_Val(const __m128& a, const __m128& b) {
    __m128 m = _mm_mul_ps(a, b); // (x, y, z, 0)
    __m128 shuf1 = _mm_shuffle_ps(m, m, _MM_SHUFFLE(2, 3, 0, 1)); // (y, x, 0, z)
    __m128 sum1 = _mm_add_ps(m, shuf1); // (x+y, y+x, z, z)
    __m128 shuf2 = _mm_shuffle_ps(sum1, sum1, _MM_SHUFFLE(0, 0, 2, 2)); // (z, z, x+y, x+y)
    return _mm_add_ps(sum1, shuf2); // (x+y+z, y+x+z, z+x+y, z+x+y)
}

template <typename IndexType>
static inline char SSE2_RayTriangleIntersection(const float* ray, const float* vertices, const IndexType* indices, float* outT, float* outUV, float margin, char (__cdecl* orig_fn)(const float*, const float*, const IndexType*, float*, float*, float)) {
    __try {
        if (!ray || !vertices || !indices) {
            if (orig_fn) return orig_fn(ray, vertices, indices, outT, outUV, margin);
            return 0;
        }
        
        // Pointer validation
        if ((uintptr_t)ray < 0x10000 || (uintptr_t)ray >= 0xBFFF0000 ||
            (uintptr_t)vertices < 0x10000 || (uintptr_t)vertices >= 0xBFFF0000 ||
            (uintptr_t)indices < 0x10000 || (uintptr_t)indices >= 0xBFFF0000 ||
            (outT && ((uintptr_t)outT < 0x10000 || (uintptr_t)outT >= 0xBFFF0000)) ||
            (outUV && ((uintptr_t)outUV < 0x10000 || (uintptr_t)outUV >= 0xBFFF0000))) {
            if (orig_fn) return orig_fn(ray, vertices, indices, outT, outUV, margin);
            return 0;
        }

        IndexType idx0 = indices[0];
        IndexType idx1 = indices[1];
        IndexType idx2 = indices[2];

        const float* v0 = vertices + 3 * idx0;
        const float* v1 = vertices + 3 * idx1;
        const float* v2 = vertices + 3 * idx2;

        // Verify vertex pointers are valid
        if ((uintptr_t)v0 < 0x10000 || (uintptr_t)v0 >= 0xBFFF0000 ||
            (uintptr_t)v1 < 0x10000 || (uintptr_t)v1 >= 0xBFFF0000 ||
            (uintptr_t)v2 < 0x10000 || (uintptr_t)v2 >= 0xBFFF0000) {
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

static void __fastcall Hooked_QuatNormalize(float* ecx, void* edx) {
    g_quatNormCalls++;
    SSE2_QuatNormalize(ecx);
}
#endif

#if !TEST_DISABLE_FRUSTUM_CULL
typedef int (__fastcall *IsAABBVisible_t)(void* ecx, void* edx, const float* bounds);
static IsAABBVisible_t orig_IsAABBVisible = nullptr;

static int __fastcall Hooked_IsAABBVisible(void* ecx, void* edx, const float* bounds) {
    g_frustumCalls++;
    int res = SSE2_IsAABBVisible((const float*)ecx, bounds);
    if (res == 0) {
        g_frustumCulled++;
    }
    return res;
}

typedef int (__fastcall *IsAABBVisibleType2_t)(void* ecx, void* edx, const float* bounds);
static IsAABBVisibleType2_t orig_IsAABBVisibleType2 = nullptr;

static int __fastcall Hooked_IsAABBVisibleType2(void* ecx, void* edx, const float* bounds) {
    g_frustumCalls++;
    int res = SSE2_IsAABBVisible_Type2((const float*)ecx, bounds);
    if (res == 0) {
        g_frustumCulled++;
    }
    return res;
}

typedef void (__fastcall *IsPointVisible_t)(void* ecx, void* edx, const float* point, uint8_t* outMask);
static IsPointVisible_t orig_IsPointVisible = nullptr;

static void __fastcall Hooked_IsPointVisible(void* ecx, void* edx, const float* point, uint8_t* outMask) {
    g_frustumCalls++;
    SSE2_IsPointVisible((const float*)ecx, point, outMask);
    if (outMask && *outMask != 0) {
        g_frustumCulled++;
    }
}
#endif

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
#else
        Log("[SimdHooks] Ray-Triangle 32-bit hook DISABLED by TEST_DISABLE_RAY_TRIANGLE_SSE2");
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
#else
        Log("[SimdHooks] Ray-Triangle 16-bit hook DISABLED by TEST_DISABLE_RAY_TRIANGLE_SSE2");
#endif
    } else {
        Log("[SimdHooks] Ray-Triangle 16-bit: fill ADDR_WOW_RAY_TRIANGLE_16BIT");
    }

    return true;
}

void ShutdownSimdHooks(void) {
    Log("[SimdHooks] Stats: matMul=%lld, ... frustum=%lld (culled=%lld, %.1f%%), rayTri=%lld (hit=%lld, %.1f%%)",
        g_matMulCalls,
        g_frustumCalls, g_frustumCulled,
        g_frustumCalls ? 100.0 * g_frustumCulled / g_frustumCalls : 0.0,
        g_rayTriangleCalls, g_rayTriangleIntersects,
        g_rayTriangleCalls ? 100.0 * g_rayTriangleIntersects / g_rayTriangleCalls : 0.0);
}
