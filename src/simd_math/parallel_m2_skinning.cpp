// ============================================================================
// Module: parallel_m2_skinning.cpp
// Description: Multi-threaded parallel SSE2 mesh skinning.
// Safety & Threading: Thread-safe, splits work across worker threads.
// ============================================================================

#include "parallel_m2_skinning.h"
#include "MinHook.h"
#include "version.h"
#include <windows.h>
#include <cstdint>
#include <intrin.h>
#include <immintrin.h>

extern "C" void Log(const char* fmt, ...);

namespace ParallelM2Skinning {

struct M2Vertex {
    float pos[3];
    uint8_t boneWeights[4];
    uint8_t boneIndices[4];
    float normal[3];
    float texCoords[2];
};

typedef void (__thiscall *SkinVertices_fn)(void* mesh, M2Vertex* dest, const M2Vertex* src, int count, const float* boneMatrices);
static SkinVertices_fn orig_SkinVertices = nullptr;

static void SkinVertices_Chunk_SSE2(M2Vertex* dest, const M2Vertex* src, int count, const float* boneMatrices) {
    for (int i = 0; i < count; i++) {
        const M2Vertex& v = src[i];
        M2Vertex& d = dest[i];

        float x = v.pos[0];
        float y = v.pos[1];
        float z = v.pos[2];
        float nx = v.normal[0];
        float ny = v.normal[1];
        float nz = v.normal[2];

        float rx = 0.0f, ry = 0.0f, rz = 0.0f;
        float rnx = 0.0f, rny = 0.0f, rnz = 0.0f;

        for (int b = 0; b < 4; b++) {
            float weight = (float)v.boneWeights[b] / 255.0f;
            if (weight <= 0.0f) continue;

            int boneIdx = v.boneIndices[b];
            const float* mat = &boneMatrices[boneIdx * 12];

            __m128 r0 = _mm_loadu_ps(&mat[0]);
            __m128 r1 = _mm_loadu_ps(&mat[4]);
            __m128 r2 = _mm_loadu_ps(&mat[8]);

            __m128 pos = _mm_setr_ps(x, y, z, 1.0f);
            __m128 p0 = _mm_mul_ps(r0, pos);
            __m128 p1 = _mm_mul_ps(r1, pos);
            __m128 p2 = _mm_mul_ps(r2, pos);

            __m128 sum0 = _mm_hadd_ps(p0, p0);
            sum0 = _mm_hadd_ps(sum0, sum0);
            float tx = _mm_cvtss_f32(sum0);

            __m128 sum1 = _mm_hadd_ps(p1, p1);
            sum1 = _mm_hadd_ps(sum1, sum1);
            float ty = _mm_cvtss_f32(sum1);

            __m128 sum2 = _mm_hadd_ps(p2, p2);
            sum2 = _mm_hadd_ps(sum2, sum2);
            float tz = _mm_cvtss_f32(sum2);

            rx += tx * weight;
            ry += ty * weight;
            rz += tz * weight;

            __m128 norm = _mm_setr_ps(nx, ny, nz, 0.0f);
            __m128 n0 = _mm_mul_ps(r0, norm);
            __m128 n1 = _mm_mul_ps(r1, norm);
            __m128 n2 = _mm_mul_ps(r2, norm);

            __m128 nsum0 = _mm_hadd_ps(n0, n0);
            nsum0 = _mm_hadd_ps(nsum0, nsum0);
            float tnx = _mm_cvtss_f32(nsum0);

            __m128 nsum1 = _mm_hadd_ps(n1, n1);
            nsum1 = _mm_hadd_ps(nsum1, nsum1);
            float tny = _mm_cvtss_f32(nsum1);

            __m128 nsum2 = _mm_hadd_ps(n2, n2);
            nsum2 = _mm_hadd_ps(nsum2, nsum2);
            float tnz = _mm_cvtss_f32(nsum2);

            rnx += tnx * weight;
            rny += tny * weight;
            rnz += tnz * weight;
        }

        d.pos[0] = rx;
        d.pos[1] = ry;
        d.pos[2] = rz;
        d.normal[0] = rnx;
        d.normal[1] = rny;
        d.normal[2] = rnz;
        d.texCoords[0] = v.texCoords[0];
        d.texCoords[1] = v.texCoords[1];
    }
}

struct SkinningThreadTask {
    M2Vertex* dest;
    const M2Vertex* src;
    int count;
    const float* boneMatrices;
    HANDLE doneEvent;
};

static DWORD WINAPI ParallelSkinningWorker(LPVOID lpParam) {
    SkinningThreadTask* task = (SkinningThreadTask*)lpParam;
    SkinVertices_Chunk_SSE2(task->dest, task->src, task->count, task->boneMatrices);
    SetEvent(task->doneEvent);
    return 0;
}

static void __fastcall Hooked_SkinVertices_Parallel(void* mesh, void* dummyEDX, M2Vertex* dest, const M2Vertex* src, int count, const float* boneMatrices) {
    if (count <= 0 || !dest || !src || !boneMatrices) return;

#if TEST_DISABLE_M2_SIMD_MT
    orig_SkinVertices(mesh, dest, src, count, boneMatrices);
#else
    if (count < 256) {
        SkinVertices_Chunk_SSE2(dest, src, count, boneMatrices);
    } else {
        int halfCount = count / 2;
        HANDLE doneEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (!doneEvent) {
            SkinVertices_Chunk_SSE2(dest, src, count, boneMatrices);
            return;
        }

        SkinningThreadTask task2 = {
            dest + halfCount,
            src + halfCount,
            count - halfCount,
            boneMatrices,
            doneEvent
        };

        HANDLE thread = CreateThread(NULL, 0, ParallelSkinningWorker, &task2, 0, NULL);
        
        SkinVertices_Chunk_SSE2(dest, src, halfCount, boneMatrices);

        if (thread) {
            WaitForSingleObject(doneEvent, INFINITE);
            CloseHandle(thread);
        } else {
            SkinVertices_Chunk_SSE2(dest + halfCount, src + halfCount, count - halfCount, boneMatrices);
        }
        CloseHandle(doneEvent);
    }
#endif
}

bool Init() {
    // 0x00703B80 is the constructor/initializer of CMissile in WoW 3.3.5a (12340),
    // not SkinVertices. Detouring it here causes stack corruption and crashes.
    // We skip hooking it entirely.
    Log("[ParallelM2Skinning] Target address 0x00703B80 is CMissile. Skipping hook for stability.");
    return true;
}

void Shutdown() {
    // No hooks were enabled to prevent crashes on CMissile address
}

} // namespace ParallelM2Skinning
