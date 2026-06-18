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
            if (MH_EnableHook(h.addr) == MH_OK) {
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
        MH_EnableHook((void*)0x004C1F00) == MH_OK) {
        Log("[MatrixSSE2] Hooked MatrixMultiply at 0x004C1F00 (SSE2, IDA-verified A*B)");
    } else {
        Log("[MatrixSSE2] MatrixMultiply hook FAILED");
    }
#else
    Log("[MatrixSSE2] MatrixMultiply DISABLED via feature flag");
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

    Log("[MatrixSSE2] Stats: MatrixCopy=%ld  MatrixIdentity=%ld  MatrixMul=%ld",
        g_matcopy_calls, g_matident_calls, g_matmul_calls);
}
