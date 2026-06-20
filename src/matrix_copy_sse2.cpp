#include <windows.h>
#include <MinHook.h>
#include <cstdint>
#include <emmintrin.h>
#include "version.h"
#include "matrix_copy_sse2.h"

extern "C" void Log(const char* fmt, ...);

// ================================================================
// Statistics — plain increments, not Interlocked.
// Both hooked functions run on the main WoW thread only;
// atomic overhead would dwarf the work itself.
// ================================================================
static volatile long g_matcopy_calls = 0;
static volatile long g_matident_calls = 0;

// ================================================================
// Original function pointers
// __fastcall typedef mirrors the __thiscall ABI on x86 MSVC:
// ECX = this, EDX = unused padding.
// ================================================================
typedef float* (__fastcall* MatCopy_t)(float* self, void* edx, float* src);
typedef float* (__fastcall* MatIdentity_t)(float* self, void* edx);

// sub_4C1F00: result = A * B, all three are float[16] passed by stack (__cdecl).
typedef float* (__cdecl* MatMul_t)(float* result, float* a, float* b);

static MatCopy_t     pOrigMatCopy     = nullptr;
static MatIdentity_t pOrigMatIdentity = nullptr;
static MatMul_t      pOrigMatMul      = nullptr;
static volatile long g_matmul_calls   = 0;

typedef float* (__cdecl* MatVec3Mul_t)(float* result, const float* vec3, const float* matrix44);
typedef float* (__cdecl* MatVec4Mul_t)(float* result, const float* vec4, const float* matrix44);

static MatVec3Mul_t  pOrigMatVec3Mul  = nullptr;
static MatVec4Mul_t  pOrigMatVec4Mul  = nullptr;
static volatile long g_matvec3_calls  = 0;
static volatile long g_matvec4_calls  = 0;

// ================================================================
// Precomputed identity matrix rows for the SSE2 store path
// ================================================================
static const __m128 kIdentityRow0 = { 1.0f, 0.0f, 0.0f, 0.0f };
static const __m128 kIdentityRow1 = { 0.0f, 1.0f, 0.0f, 0.0f };
static const __m128 kIdentityRow2 = { 0.0f, 0.0f, 1.0f, 0.0f };
static const __m128 kIdentityRow3 = { 0.0f, 0.0f, 0.0f, 1.0f };

// ================================================================
// sub_407F80: 4x4 matrix copy (247 xrefs)
// Original does 16 scalar FPU load/store pairs.
// 4x SSE2 unaligned 128-bit moves cover all 64 bytes.
// ================================================================
static float* __fastcall HookMatrixCopy(float* self, void* /*edx*/, float* src) {
    ++g_matcopy_calls;

    uintptr_t s = (uintptr_t)self;
    uintptr_t p = (uintptr_t)src;
    if (s > 0x10000 && s < 0xBFFF0000 &&
        p > 0x10000 && p < 0xBFFF0000) {
        __try {
            _mm_storeu_ps(self,      _mm_loadu_ps(src));
            _mm_storeu_ps(self + 4,  _mm_loadu_ps(src + 4));
            _mm_storeu_ps(self + 8,  _mm_loadu_ps(src + 8));
            _mm_storeu_ps(self + 12, _mm_loadu_ps(src + 12));
            return self;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            // Bad pointer during load/store (e.g. unmapped page)
        }
    }

    return pOrigMatCopy(self, nullptr, src);
}

// ================================================================
// sub_407F40: 4x4 matrix identity (53 xrefs)
// Original writes 16 immediate floats through the FPU.
// 4x SSE2 stores from compile-time constants.
// ================================================================
static float* __fastcall HookMatrixIdentity(float* self, void* /*edx*/) {
    ++g_matident_calls;

    uintptr_t s = (uintptr_t)self;
    if (s > 0x10000 && s < 0xBFFF0000) {
        __try {
            _mm_storeu_ps(self,      kIdentityRow0);
            _mm_storeu_ps(self + 4,  kIdentityRow1);
            _mm_storeu_ps(self + 8,  kIdentityRow2);
            _mm_storeu_ps(self + 12, kIdentityRow3);
            return self;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            // Bad pointer during store
        }
    }

    return pOrigMatIdentity(self, nullptr);
}

// ================================================================
// sub_4C1F00: 4x4 matrix multiply  result = A * B  (53+ xrefs)
// ================================================================
// IDA-verified convention: result[r*4+c] = sum_k A[r*4+k] * B[k*4+c]
// (row-major C = A*B). The SSE2 form below broadcasts each element of an
// A row across a full B row and accumulates, producing the identical
// products; only the summation order differs, a sub-ULP delta that is
// invisible for transform matrices. It loads all of B and a full A row
// before storing, so it is also safe when result aliases A or B (the
// scalar original is not, but no caller passes aliasing pointers).
static float* __cdecl HookMatrixMultiply(float* result, float* a, float* b) {
    ++g_matmul_calls;

    uintptr_t r = (uintptr_t)result, pa = (uintptr_t)a, pb = (uintptr_t)b;
    if (r > 0x10000 && r < 0xBFFF0000 &&
        pa > 0x10000 && pa < 0xBFFF0000 &&
        pb > 0x10000 && pb < 0xBFFF0000) {
        __try {
            __m128 b0 = _mm_loadu_ps(b);
            __m128 b1 = _mm_loadu_ps(b + 4);
            __m128 b2 = _mm_loadu_ps(b + 8);
            __m128 b3 = _mm_loadu_ps(b + 12);
            for (int row = 0; row < 4; ++row) {
                __m128 ar = _mm_loadu_ps(a + row * 4);
                __m128 acc = _mm_mul_ps(_mm_shuffle_ps(ar, ar, _MM_SHUFFLE(0,0,0,0)), b0);
                acc = _mm_add_ps(acc, _mm_mul_ps(_mm_shuffle_ps(ar, ar, _MM_SHUFFLE(1,1,1,1)), b1));
                acc = _mm_add_ps(acc, _mm_mul_ps(_mm_shuffle_ps(ar, ar, _MM_SHUFFLE(2,2,2,2)), b2));
                acc = _mm_add_ps(acc, _mm_mul_ps(_mm_shuffle_ps(ar, ar, _MM_SHUFFLE(3,3,3,3)), b3));
                _mm_storeu_ps(result + row * 4, acc);
            }
            return result;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            // Unmapped page mid-op — fall through to the original.
        }
    }
    return pOrigMatMul(result, a, b);
}

// ================================================================
// sub_4C21B0: 3D point * 4x4 matrix (100+ xrefs)
// Vectorized via column linear combination using SSE2
// ================================================================
static float* __cdecl Hooked_MatVec3Mul(float* result, const float* vec3, const float* matrix44) {
    ++g_matvec3_calls;

    uintptr_t r = (uintptr_t)result, pv = (uintptr_t)vec3, pm = (uintptr_t)matrix44;
    if (r > 0x10000 && r < 0xBFFF0000 &&
        pv > 0x10000 && pv < 0xBFFF0000 &&
        pm > 0x10000 && pm < 0xBFFF0000) {
        __try {
            __m128 vx = _mm_set1_ps(vec3[0]);
            __m128 vy = _mm_set1_ps(vec3[1]);
            __m128 vz = _mm_set1_ps(vec3[2]);

            __m128 col0 = _mm_loadu_ps(matrix44);
            __m128 col1 = _mm_loadu_ps(matrix44 + 4);
            __m128 col2 = _mm_loadu_ps(matrix44 + 8);
            __m128 col3 = _mm_loadu_ps(matrix44 + 12);

            __m128 res = _mm_add_ps(
                _mm_add_ps(_mm_mul_ps(vx, col0), _mm_mul_ps(vy, col1)),
                _mm_add_ps(_mm_mul_ps(vz, col2), col3)
            );

            // Safe 12-byte write to result (float[3])
            _mm_store_ss(result, res);
            _mm_store_ss(result + 1, _mm_shuffle_ps(res, res, _MM_SHUFFLE(1, 1, 1, 1)));
            _mm_store_ss(result + 2, _mm_shuffle_ps(res, res, _MM_SHUFFLE(2, 2, 2, 2)));

            return result;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            // Unmapped page - fallback
        }
    }
    return pOrigMatVec3Mul(result, vec3, matrix44);
}

// ================================================================
// sub_4C2270: 4D vector * 4x4 matrix (20 xrefs)
// Vectorized via column linear combination using SSE2
// ================================================================
static float* __cdecl Hooked_MatVec4Mul(float* result, const float* vec4, const float* matrix44) {
    ++g_matvec4_calls;

    uintptr_t r = (uintptr_t)result, pv = (uintptr_t)vec4, pm = (uintptr_t)matrix44;
    if (r > 0x10000 && r < 0xBFFF0000 &&
        pv > 0x10000 && pv < 0xBFFF0000 &&
        pm > 0x10000 && pm < 0xBFFF0000) {
        __try {
            __m128 vx = _mm_set1_ps(vec4[0]);
            __m128 vy = _mm_set1_ps(vec4[1]);
            __m128 vz = _mm_set1_ps(vec4[2]);
            __m128 vw = _mm_set1_ps(vec4[3]);

            __m128 col0 = _mm_loadu_ps(matrix44);
            __m128 col1 = _mm_loadu_ps(matrix44 + 4);
            __m128 col2 = _mm_loadu_ps(matrix44 + 8);
            __m128 col3 = _mm_loadu_ps(matrix44 + 12);

            __m128 res = _mm_add_ps(
                _mm_add_ps(_mm_mul_ps(vx, col0), _mm_mul_ps(vy, col1)),
                _mm_add_ps(_mm_mul_ps(vz, col2), _mm_mul_ps(vw, col3))
            );

            // Safe 16-byte write to result (float[4])
            _mm_storeu_ps(result, res);
            return result;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            // Unmapped page - fallback
        }
    }
    return pOrigMatVec4Mul(result, vec4, matrix44);
}

// ================================================================
// sub_4C3420 / sub_4C3600: C3Vector::Normalize (in-place, __thiscall(this))
// ================================================================
// Both do v *= 1.0/sqrt(x*x+y*y+z*z) with x87 fsqrt+fdiv. We replace that with
// full-precision SSE (sqrtss + divss) -- deliberately NOT _mm_rsqrt_ps, whose
// approximation + the missing degenerate guard is exactly what NaN-poisoned the
// quaternion-normalize hook. sqrtss/divss are IEEE round-to-nearest, so the result
// matches the scalar original to sub-ULP (only the x^2+y^2+z^2 accumulation order
// differs, x87's 80-bit vs SSE 32-bit -- invisible for a unit vector).
//
//   sub_4C3420: no guard. On a zero vector the original yields 1.0/0 = +Inf then
//               v*Inf = NaN; SSE divss-by-zero (exceptions masked, as WoW runs)
//               produces the identical Inf/NaN, so behaviour is faithful.
//   sub_4C3600: guarded -- only normalizes when mag^2 > 2^-22, else leaves the
//               vector unchanged. Replicated exactly.
#if !TEST_DISABLE_VEC_NORMALIZE_SSE2
typedef void (__fastcall* Vec3Norm_t)(float* self, void* edx);
static Vec3Norm_t pOrigVec3Norm     = nullptr;  // sub_4C3420 (unguarded)
static Vec3Norm_t pOrigVec3NormSafe = nullptr;  // sub_4C3600 (mag^2 > 2^-22 guard)
static volatile long g_vec3norm_calls = 0;

// 2^-22 == 0x34000000f, the engine's near-zero magnitude cutoff in sub_4C3600.
static const float kVec3NormEps = 0.00000023841858f;

static inline void SSE2_Vec3NormalizeInPlace(float* v, bool guard) {
    // Read exactly 3 floats (never v[3], which may sit on an unmapped next page).
    __m128 xyz = _mm_setr_ps(v[0], v[1], v[2], 0.0f);
    __m128 sq  = _mm_mul_ps(xyz, xyz);                                  // x^2,y^2,z^2,0
    __m128 mag2 = _mm_add_ss(_mm_add_ss(sq,
                      _mm_shuffle_ps(sq, sq, _MM_SHUFFLE(1, 1, 1, 1))), // +y^2
                      _mm_shuffle_ps(sq, sq, _MM_SHUFFLE(2, 2, 2, 2))); // +z^2
    if (guard) {
        float m; _mm_store_ss(&m, mag2);
        if (!(m > kVec3NormEps)) return;  // leave unchanged, exactly like the engine
    }
    __m128 inv  = _mm_div_ss(_mm_set_ss(1.0f), _mm_sqrt_ss(mag2));      // 1.0/sqrt(mag2)
    __m128 invb = _mm_shuffle_ps(inv, inv, _MM_SHUFFLE(0, 0, 0, 0));    // broadcast
    __m128 res  = _mm_mul_ps(xyz, invb);
    _mm_store_ss(v,     res);                                           // 12-byte store
    _mm_store_ss(v + 1, _mm_shuffle_ps(res, res, _MM_SHUFFLE(1, 1, 1, 1)));
    _mm_store_ss(v + 2, _mm_shuffle_ps(res, res, _MM_SHUFFLE(2, 2, 2, 2)));
}

static void __fastcall Hooked_Vec3Norm(float* self, void* edx) {
    ++g_vec3norm_calls;
    if ((uintptr_t)self > 0x10000 && (uintptr_t)self < 0xBFFF0000) {
        __try {
            SSE2_Vec3NormalizeInPlace(self, false);
            return;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Bad pointer surfaced during the read -- nothing was written yet
            // (the fault is on the initial load), so deferring to the original
            // leaves the vector exactly as the engine would handle it.
        }
    }
    pOrigVec3Norm(self, edx);
}

static void __fastcall Hooked_Vec3NormSafe(float* self, void* edx) {
    ++g_vec3norm_calls;
    if ((uintptr_t)self > 0x10000 && (uintptr_t)self < 0xBFFF0000) {
        __try {
            SSE2_Vec3NormalizeInPlace(self, true);
            return;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    pOrigVec3NormSafe(self, edx);
}
#endif

// ================================================================
// Install hooks
// ================================================================
bool InstallMatrixCopySSE2() {
    struct HookDef {
        void*       addr;
        void*       hook;
        void**      orig;
        const char* name;
        uint32_t    xrefs;
    };

    HookDef hooks[] = {
        { (void*)0x00407F80, (void*)HookMatrixCopy,     (void**)&pOrigMatCopy,     "MatrixCopy",     247 },
        { (void*)0x00407F40, (void*)HookMatrixIdentity, (void**)&pOrigMatIdentity, "MatrixIdentity",  53 },
    };

    int installed = 0;
    for (auto& h : hooks) {
        if (WineSafe_CreateHook(h.addr, h.hook, h.orig) == MH_OK) {
            if (WO_EnableHook(h.addr) == MH_OK) {
                installed++;
                Log("[MatrixSSE2] Hooked %s at 0x%08X (%d xrefs)", h.name, (DWORD)(uintptr_t)h.addr, h.xrefs);
            }
        }
    }

    Log("[MatrixSSE2] Installed %d/%d hooks (total %d xrefs)",
        installed, (int)(sizeof(hooks) / sizeof(hooks[0])),
        247 + 53);

#if !TEST_DISABLE_MATRIX_MULTIPLY
    if (WineSafe_CreateHook((void*)0x004C1F00, (void*)HookMatrixMultiply,
                            (void**)&pOrigMatMul) == MH_OK &&
        WO_EnableHook((void*)0x004C1F00) == MH_OK) {
        Log("[MatrixSSE2] Hooked MatrixMultiply at 0x004C1F00 (SSE2, IDA-verified A*B)");
    } else {
        Log("[MatrixSSE2] MatrixMultiply hook FAILED");
    }
#else
    Log("[MatrixSSE2] MatrixMultiply DISABLED via feature flag");
#endif

#if !TEST_DISABLE_MATRIX_VECTOR_SSE2
    if (WineSafe_CreateHook((void*)0x004C21B0, (void*)Hooked_MatVec3Mul,
                            (void**)&pOrigMatVec3Mul) == MH_OK &&
        WO_EnableHook((void*)0x004C21B0) == MH_OK) {
        Log("[MatrixSSE2] Hooked MatVec3Mul at 0x004C21B0 (SSE2, 100+ xrefs)");
    } else {
        Log("[MatrixSSE2] MatVec3Mul hook FAILED");
    }

    if (WineSafe_CreateHook((void*)0x004C2270, (void*)Hooked_MatVec4Mul,
                            (void**)&pOrigMatVec4Mul) == MH_OK &&
        WO_EnableHook((void*)0x004C2270) == MH_OK) {
        Log("[MatrixSSE2] Hooked MatVec4Mul at 0x004C2270 (SSE2, 20 xrefs)");
    } else {
        Log("[MatrixSSE2] MatVec4Mul hook FAILED");
    }
#else
    Log("[MatrixSSE2] Matrix-Vector hooks DISABLED via feature flag");
#endif

#if !TEST_DISABLE_VEC_NORMALIZE_SSE2
    if (WineSafe_CreateHook((void*)0x004C3420, (void*)Hooked_Vec3Norm,
                            (void**)&pOrigVec3Norm) == MH_OK &&
        WO_EnableHook((void*)0x004C3420) == MH_OK) {
        Log("[MatrixSSE2] Hooked C3Vector::Normalize at 0x004C3420 (SSE2 sqrtss, 12 callers)");
    } else {
        Log("[MatrixSSE2] C3Vector::Normalize hook FAILED");
    }

    if (WineSafe_CreateHook((void*)0x004C3600, (void*)Hooked_Vec3NormSafe,
                            (void**)&pOrigVec3NormSafe) == MH_OK &&
        WO_EnableHook((void*)0x004C3600) == MH_OK) {
        Log("[MatrixSSE2] Hooked C3Vector::Normalize(guarded) at 0x004C3600 (SSE2 sqrtss, 2^-22 guard, 22 callers)");
    } else {
        Log("[MatrixSSE2] C3Vector::Normalize(guarded) hook FAILED");
    }
#else
    Log("[MatrixSSE2] Vector-Normalize hooks DISABLED via feature flag");
#endif

    return installed == (int)(sizeof(hooks) / sizeof(hooks[0]));
}

// ================================================================
// Cleanup
// ================================================================
void ShutdownMatrixCopySSE2() {
    MH_DisableHook((void*)0x00407F80);
    MH_DisableHook((void*)0x00407F40);
#if !TEST_DISABLE_MATRIX_MULTIPLY
    MH_DisableHook((void*)0x004C1F00);
#endif
#if !TEST_DISABLE_MATRIX_VECTOR_SSE2
    MH_DisableHook((void*)0x004C21B0);
    MH_DisableHook((void*)0x004C2270);
#endif
#if !TEST_DISABLE_VEC_NORMALIZE_SSE2
    MH_DisableHook((void*)0x004C3420);
    MH_DisableHook((void*)0x004C3600);
    Log("[MatrixSSE2] Stats: Vec3Normalize=%ld", g_vec3norm_calls);
#endif

    Log("[MatrixSSE2] Stats: MatrixCopy=%ld  MatrixIdentity=%ld  MatrixMul=%ld  MatVec3=%ld  MatVec4=%ld",
        g_matcopy_calls, g_matident_calls, g_matmul_calls, g_matvec3_calls, g_matvec4_calls);
}
