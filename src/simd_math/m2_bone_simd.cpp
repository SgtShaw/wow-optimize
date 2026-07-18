// ============================================================================
// Module: m2_bone_simd.cpp
// Description: Multi-Threaded M2 Skeleton Animations & SIMD Bone Math
// Safety & Threading: 64-slot ring buffer task queue with per-slot sync.
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
// Multi-Threaded UpdateBones Hook (64-Slot Ring Buffer)
// ================================================================
#if !TEST_DISABLE_M2_BONE_MT
typedef int (__thiscall* UpdateBones_fn)(void* pThis, float* a2, int a3, float a4, float a5, float a6);
static UpdateBones_fn orig_UpdateBones = nullptr;

static constexpr int WORKER_COUNT = 4;
static constexpr int RING_SLOTS = 64;

struct BoneRingSlot {
    void* pThis;
    float* a2;
    int a3;
    float a4;
    float a5;
    float a6;
    int result;
    HANDLE doneEvent;
    volatile LONG active;
};

static BoneRingSlot g_ring[RING_SLOTS] = {};
static HANDLE g_workerThreads[WORKER_COUNT] = { NULL };
static HANDLE g_queueSemaphore = NULL;
static SRWLOCK g_queueLock = SRWLOCK_INIT;
static bool g_threadsRunning = false;
static volatile LONG g_ringIndex = 0;

static int g_slotQueue[RING_SLOTS] = {};
static int g_qHead = 0, g_qTail = 0, g_qCount = 0;

static DWORD WINAPI M2WorkerProc(LPVOID lpParam) {
    while (g_threadsRunning) {
        WaitForSingleObject(g_queueSemaphore, INFINITE);
        if (!g_threadsRunning) break;

        int slotIdx = -1;
        AcquireSRWLockExclusive(&g_queueLock);
        if (g_qCount > 0) {
            slotIdx = g_slotQueue[g_qHead];
            g_qHead = (g_qHead + 1) % RING_SLOTS;
            g_qCount--;
        }
        ReleaseSRWLockExclusive(&g_queueLock);

        if (slotIdx >= 0 && slotIdx < RING_SLOTS) {
            BoneRingSlot* slot = &g_ring[slotIdx];
            __try {
                if (orig_UpdateBones && slot->pThis) {
                    slot->result = orig_UpdateBones(
                        slot->pThis, slot->a2, slot->a3, slot->a4, slot->a5, slot->a6
                    );
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}

            SetEvent(slot->doneEvent);
        }
    }
    return 0;
}

static int __fastcall Hooked_UpdateBones(void* pThis, void* edx, float* a2, int a3, float a4, float a5, float a6) {
    if (!pThis || (uintptr_t)pThis < 0x10000 || (uintptr_t)pThis >= 0xFFE00000) {
        return orig_UpdateBones ? orig_UpdateBones(pThis, a2, a3, a4, a5, a6) : 0;
    }

    __try {
        uint16_t boneCount = *(uint16_t*)((uint8_t*)pThis + 72);

        if (boneCount >= 8 && g_threadsRunning) {
            int slotIdx = (int)(InterlockedIncrement(&g_ringIndex) % RING_SLOTS);
            BoneRingSlot* slot = &g_ring[slotIdx];

            if (InterlockedCompareExchange(&slot->active, 1, 0) == 0) {
                slot->pThis  = pThis;
                slot->a2     = a2;
                slot->a3     = a3;
                slot->a4     = a4;
                slot->a5     = a5;
                slot->a6     = a6;
                slot->result = 0;
                ResetEvent(slot->doneEvent);

                AcquireSRWLockExclusive(&g_queueLock);
                if (g_qCount < RING_SLOTS) {
                    g_slotQueue[g_qTail] = slotIdx;
                    g_qTail = (g_qTail + 1) % RING_SLOTS;
                    g_qCount++;
                    ReleaseSRWLockExclusive(&g_queueLock);
                    ReleaseSemaphore(g_queueSemaphore, 1, NULL);

                    WaitForSingleObject(slot->doneEvent, 10);
                    int res = slot->result;
                    InterlockedExchange(&slot->active, 0);
                    return res;
                }
                ReleaseSRWLockExclusive(&g_queueLock);
                InterlockedExchange(&slot->active, 0);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    return orig_UpdateBones(pThis, a2, a3, a4, a5, a6);
}
#endif

bool Init() {
    Log("[M2BoneSimd] Active - CPU SSE2 Bone Matrix Acceleration Initialized");

#if !TEST_DISABLE_M2_BONE_MT
    if (Config::g_settings.OptM2BoneMt) {
        g_queueSemaphore = CreateSemaphore(NULL, 0, RING_SLOTS, NULL);
        g_threadsRunning = true;

        for (int i = 0; i < RING_SLOTS; i++) {
            g_ring[i].doneEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
            g_ring[i].active = 0;
        }

        for (int i = 0; i < WORKER_COUNT; i++) {
            g_workerThreads[i] = CreateThread(NULL, 0, M2WorkerProc, NULL, 0, NULL);
        }

        if (WineSafe_CreateHook((void*)0x0082F0F0, (void*)Hooked_UpdateBones, (void**)&orig_UpdateBones) == MH_OK) {
            if (WO_EnableHook((void*)0x0082F0F0) == MH_OK) {
                Log("[M2BoneSimd] Multi-Threaded UpdateBones hook at 0x0082F0F0 ACTIVE (64 ring slots)");
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
        ReleaseSemaphore(g_queueSemaphore, WORKER_COUNT, NULL);
        for (int i = 0; i < WORKER_COUNT; i++) {
            if (g_workerThreads[i]) {
                WaitForSingleObject(g_workerThreads[i], 100);
                CloseHandle(g_workerThreads[i]);
                g_workerThreads[i] = NULL;
            }
        }
        for (int i = 0; i < RING_SLOTS; i++) {
            if (g_ring[i].doneEvent) CloseHandle(g_ring[i].doneEvent);
        }
        if (g_queueSemaphore) CloseHandle(g_queueSemaphore);
    }
    MH_DisableHook((void*)0x0082F0F0);
#endif
}

} // namespace M2BoneSimd
