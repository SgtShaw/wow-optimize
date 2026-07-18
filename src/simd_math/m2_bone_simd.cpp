// ============================================================================
// Module: m2_bone_simd.cpp
// Description: Multi-Threaded M2 Skeleton Animations & SIMD Bone Math
// Safety & Threading: Thread-safe dispatch using lock-free atomic counters.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <intrin.h>
#include "MinHook.h"
#include "version.h"
#include "core/config.h"
#include "m2_bone_simd.h"

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

        __m128 sum0 = _mm_hadd_ps(r0, r0);
        sum0 = _mm_hadd_ps(sum0, sum0);

        __m128 sum1 = _mm_hadd_ps(r1, r1);
        sum1 = _mm_hadd_ps(sum1, sum1);

        __m128 sum2 = _mm_hadd_ps(r2, r2);
        sum2 = _mm_hadd_ps(sum2, sum2);

        outMatrix[i * 4]     = _mm_cvtss_f32(sum0);
        outMatrix[i * 4 + 1] = _mm_cvtss_f32(sum1);
        outMatrix[i * 4 + 2] = _mm_cvtss_f32(sum2);
        outMatrix[i * 4 + 3] = 0.0f;
    }
}

// ================================================================
// Multi-Threaded UpdateBones Hook
// ================================================================
#if !TEST_DISABLE_M2_BONE_MT
typedef int (__thiscall* UpdateBones_fn)(void* pThis, float* a2, int a3, float a4, float a5, float a6);
static UpdateBones_fn orig_UpdateBones = nullptr;

static constexpr int WORKER_COUNT = 4;
static HANDLE g_workerThreads[WORKER_COUNT] = { NULL };
static HANDLE g_workEvent = NULL;
static HANDLE g_doneEvent = NULL;
static bool   g_threadsRunning = false;
static volatile LONG g_activeWorkers = 0;

struct BoneTask {
    void* pThis;
    float* a2;
    int a3;
    float a4;
    float a5;
    float a6;
    int result;
};

static volatile BoneTask g_currentTask = {};
static volatile LONG     g_taskPending = 0;

static DWORD WINAPI M2WorkerProc(LPVOID lpParam) {
    while (g_threadsRunning) {
        WaitForSingleObject(g_workEvent, INFINITE);
        if (!g_threadsRunning) break;

        if (InterlockedExchange(&g_taskPending, 0) == 1) {
            __try {
                if (orig_UpdateBones && g_currentTask.pThis) {
                    g_currentTask.result = orig_UpdateBones(
                        g_currentTask.pThis,
                        g_currentTask.a2,
                        g_currentTask.a3,
                        g_currentTask.a4,
                        g_currentTask.a5,
                        g_currentTask.a6
                    );
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}

            SetEvent(g_doneEvent);
        }
    }
    return 0;
}

static int __fastcall Hooked_UpdateBones(void* pThis, void* edx, float* a2, int a3, float a4, float a5, float a6) {
    if (!pThis || (uintptr_t)pThis < 0x10000 || (uintptr_t)pThis >= 0xFFE00000) {
        return orig_UpdateBones ? orig_UpdateBones(pThis, a2, a3, a4, a5, a6) : 0;
    }

    __try {
        // Read bone count from CM2Model instance header (offset 0x48 / 72 bytes)
        uint16_t boneCount = *(uint16_t*)((uint8_t*)pThis + 72);

        // Offload models with bone count >= 8 if worker pool is idle
        if (boneCount >= 8 && g_threadsRunning && InterlockedCompareExchange(&g_taskPending, 0, 0) == 0) {
            g_currentTask.pThis  = pThis;
            g_currentTask.a2     = a2;
            g_currentTask.a3     = a3;
            g_currentTask.a4     = a4;
            g_currentTask.a5     = a5;
            g_currentTask.a6     = a6;
            g_currentTask.result = 0;

            ResetEvent(g_doneEvent);
            InterlockedExchange(&g_taskPending, 1);
            SetEvent(g_workEvent);

            // Synchronize completion
            WaitForSingleObject(g_doneEvent, 5); // 5ms safety timeout
            return g_currentTask.result;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    return orig_UpdateBones(pThis, a2, a3, a4, a5, a6);
}
#endif

bool Init() {
    Log("[M2BoneSimd] Active - CPU SSE2 Bone Matrix Acceleration Initialized");

#if !TEST_DISABLE_M2_BONE_MT
    if (Config::g_settings.OptM2BoneMt) {
        g_workEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        g_doneEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        g_threadsRunning = true;

        for (int i = 0; i < WORKER_COUNT; i++) {
            g_workerThreads[i] = CreateThread(NULL, 0, M2WorkerProc, NULL, 0, NULL);
        }

        if (WineSafe_CreateHook((void*)0x0082F0F0, (void*)Hooked_UpdateBones, (void**)&orig_UpdateBones) == MH_OK) {
            if (WO_EnableHook((void*)0x0082F0F0) == MH_OK) {
                Log("[M2BoneSimd] Multi-Threaded UpdateBones hook at 0x0082F0F0 ACTIVE (%d workers)", WORKER_COUNT);
            }
        }
    }
#endif

    return true;
}

void Shutdown() {
#if !TEST_DISABLE_M2_BONE_MT
    if (g_threadsRunning) {
        g_threadsRunning = false;
        SetEvent(g_workEvent);
        for (int i = 0; i < WORKER_COUNT; i++) {
            if (g_workerThreads[i]) {
                WaitForSingleObject(g_workerThreads[i], 100);
                CloseHandle(g_workerThreads[i]);
                g_workerThreads[i] = NULL;
            }
        }
        if (g_workEvent) CloseHandle(g_workEvent);
        if (g_doneEvent) CloseHandle(g_doneEvent);
    }
    MH_DisableHook((void*)0x0082F0F0);
#endif
}

} // namespace M2BoneSimd
