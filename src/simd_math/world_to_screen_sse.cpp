#include <windows.h>
#include <intrin.h>
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace WorldToScreenSse {

struct Vector3 {
    float x, y, z;
};

struct Vector2 {
    float x, y;
};

// SIMD Vectorized World-To-Screen projection math (SSE4.1)
bool ProjectCoordinates(const Vector3& worldPos, const float* viewProjectionMatrix, int screenWidth, int screenHeight, Vector2* outScreenPos) {
    // Load world coordinate (x, y, z, 1.0f)
    __m128 pos = _mm_setr_ps(worldPos.x, worldPos.y, worldPos.z, 1.0f);

    // Load View-Projection matrix rows
    __m128 r0 = _mm_loadu_ps(&viewProjectionMatrix[0]);
    __m128 r1 = _mm_loadu_ps(&viewProjectionMatrix[4]);
    __m128 r2 = _mm_loadu_ps(&viewProjectionMatrix[8]);
    __m128 r3 = _mm_loadu_ps(&viewProjectionMatrix[12]);

    // Multiply pos by matrix rows
    __m128 m0 = _mm_mul_ps(r0, pos);
    __m128 m1 = _mm_mul_ps(r1, pos);
    __m128 m2 = _mm_mul_ps(r2, pos);
    __m128 m3 = _mm_mul_ps(r3, pos);

    // Horizontal adds
    __m128 sum0 = _mm_hadd_ps(m0, m0);
    sum0 = _mm_hadd_ps(sum0, sum0); // Clip x
    
    __m128 sum1 = _mm_hadd_ps(m1, m1);
    sum1 = _mm_hadd_ps(sum1, sum1); // Clip y

    __m128 sum3 = _mm_hadd_ps(m3, m3);
    sum3 = _mm_hadd_ps(sum3, sum3); // Clip w

    float w = _mm_cvtss_f32(sum3);
    if (w < 0.001f) return false; // Behind camera

    float cx = _mm_cvtss_f32(sum0) / w;
    float cy = _mm_cvtss_f32(sum1) / w;

    outScreenPos->x = (screenWidth / 2.0f) + (cx * screenWidth / 2.0f);
    outScreenPos->y = (screenHeight / 2.0f) - (cy * screenHeight / 2.0f);
    return true;
}

bool Init() {
    Log("[WorldToScreenSse] Active - Vectorized 3D Coordinate Projection math initialized");
    return true;
}

void Shutdown() {
    // Cleanup
}

} // namespace WorldToScreenSse
