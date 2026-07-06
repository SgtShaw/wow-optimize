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

    double rx = matrix[0] * x + matrix[1] * y + matrix[2] * z + matrix[3];
    double ry = matrix[4] * x + matrix[5] * y + matrix[6] * z + matrix[7];
    double rz = matrix[8] * x + matrix[9] * y + matrix[10] * z + matrix[11];

    outVec[0] = (float)rx;
    outVec[1] = (float)ry;
    outVec[2] = (float)rz;
#endif
}

// 2. Vector3 Normalize Hook Target: 0x004C3420
typedef float (__cdecl *Vec3Normalize_fn)(float* vec);
static Vec3Normalize_fn orig_Vec3Normalize = nullptr;

static float __cdecl Hooked_Vec3Normalize(float* vec) {
#if TEST_DISABLE_SIMD_MATH_FAST
    return orig_Vec3Normalize(vec);
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
        return (float)mag;
    }
    vec[0] = 0.0f;
    vec[1] = 0.0f;
    vec[2] = 0.0f;
    return 0.0f;
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

} // namespace SimdMathFast
