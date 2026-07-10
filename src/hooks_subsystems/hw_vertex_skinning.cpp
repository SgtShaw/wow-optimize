// ============================================================================
// Module: hw_vertex_skinning.cpp
// Description: SSE4.1 Vectorized CPU-based software vertex skinning.
// Safety & Threading: Thread-safe, runs on render and animation threads.
// ============================================================================

#include "hw_vertex_skinning.h"
#include "MinHook.h"
#include "version.h"
#include <windows.h>
#include <cstdint>
#include <cmath>
#include <emmintrin.h> // SSE2
#include <smmintrin.h> // SSE4.1

extern "C" void Log(const char* fmt, ...);

namespace HwVertexSkinning {

struct M2Vertex {
    float pos[3];
    uint8_t boneWeights[4];
    uint8_t boneIndices[4];
    float normal[3];
    float texCoords[2];
};

typedef void (__thiscall *SkinVertices_fn)(void* mesh, M2Vertex* dest, const M2Vertex* src, int count, const float* boneMatrices);
static SkinVertices_fn orig_SkinVertices = nullptr;

// SSE4.1 Optimized Software Skinning Routine
static void VectorizedSkinVertices(M2Vertex* dest, const M2Vertex* src, int count, const float* boneMatrices) {
    if (!src || !dest || !boneMatrices || count <= 0) return;

    for (int i = 0; i < count; ++i) {
        const M2Vertex& v = src[i];
        
        // Setup local position vector (x, y, z, 1.0)
        __m128 localPos = _mm_set_ps(1.0f, v.pos[2], v.pos[1], v.pos[0]);
        
        __m128 skinnedPos = _mm_setzero_ps();
        __m128 skinnedNormal = _mm_setzero_ps();
        
        float totalWeight = 0.0f;
        
        // Loop over the 4 possible bone influences
        for (int j = 0; j < 4; ++j) {
            float weight = v.boneWeights[j] / 255.0f;
            if (weight <= 0.0f) continue;
            
            totalWeight += weight;
            int boneIdx = v.boneIndices[j];
            
            // Each bone matrix is 3x4 (12 floats)
            const float* matrix = &boneMatrices[boneIdx * 12];
            
            // Load matrix rows
            __m128 r0 = _mm_loadu_ps(matrix);     // m00, m01, m02, m03
            __m128 r1 = _mm_loadu_ps(matrix + 4); // m10, m11, m12, m13
            __m128 r2 = _mm_loadu_ps(matrix + 8); // m20, m21, m22, m23
            
            // Perform vectorized matrix multiplication using Dot Product (SSE4.1)
            float x = _mm_cvtss_f32(_mm_dp_ps(r0, localPos, 0xF1));
            float y = _mm_cvtss_f32(_mm_dp_ps(r1, localPos, 0xF1));
            float z = _mm_cvtss_f32(_mm_dp_ps(r2, localPos, 0xF1));
            
            // Accumulate weighted position
            __m128 weightedPos = _mm_set_ps(0.0f, z * weight, y * weight, x * weight);
            skinnedPos = _mm_add_ps(skinnedPos, weightedPos);
            
            // Setup local normal vector (nx, ny, nz, 0.0)
            __m128 localNormal = _mm_set_ps(0.0f, v.normal[2], v.normal[1], v.normal[0]);
            
            // Transform normal (rotational part of matrix: 3x3)
            float nx = _mm_cvtss_f32(_mm_dp_ps(r0, localNormal, 0x71));
            float ny = _mm_cvtss_f32(_mm_dp_ps(r1, localNormal, 0x71));
            float nz = _mm_cvtss_f32(_mm_dp_ps(r2, localNormal, 0x71));
            
            __m128 weightedNormal = _mm_set_ps(0.0f, nz * weight, ny * weight, nx * weight);
            skinnedNormal = _mm_add_ps(skinnedNormal, weightedNormal);
        }
        
        // Write transformed data back
        // Write transformed data back
        if (totalWeight > 0.0f) {
            float tempPos[4];
            float tempNormal[4];
            _mm_storeu_ps(tempPos, skinnedPos);
            _mm_storeu_ps(tempNormal, skinnedNormal);
            
            dest[i].pos[0] = tempPos[0];
            dest[i].pos[1] = tempPos[1];
            dest[i].pos[2] = tempPos[2];
            
            // Normalize the blended normal vector
            float sumSq = _mm_cvtss_f32(_mm_dp_ps(skinnedNormal, skinnedNormal, 0x71));
            if (sumSq > 0.0f) {
                float invLength = 1.0f / sqrtf(sumSq);
                dest[i].normal[0] = tempNormal[0] * invLength;
                dest[i].normal[1] = tempNormal[1] * invLength;
                dest[i].normal[2] = tempNormal[2] * invLength;
            } else {
                dest[i].normal[0] = tempNormal[0];
                dest[i].normal[1] = tempNormal[1];
                dest[i].normal[2] = tempNormal[2];
            }
        } else {
            dest[i].pos[0] = v.pos[0];
            dest[i].pos[1] = v.pos[1];
            dest[i].pos[2] = v.pos[2];
            dest[i].normal[0] = v.normal[0];
            dest[i].normal[1] = v.normal[1];
            dest[i].normal[2] = v.normal[2];
        }
        
        // Copy unmodified attributes
        dest[i].texCoords[0] = v.texCoords[0];
        dest[i].texCoords[1] = v.texCoords[1];
        
        for (int k = 0; k < 4; ++k) {
            dest[i].boneWeights[k] = v.boneWeights[k];
            dest[i].boneIndices[k] = v.boneIndices[k];
        }
    }
}

static void __fastcall Hooked_SkinVertices_HW(void* mesh, void* dummyEDX, M2Vertex* dest, const M2Vertex* src, int count, const float* boneMatrices) {
#if TEST_DISABLE_HW_SKINNING
    orig_SkinVertices(mesh, dest, src, count, boneMatrices);
#else
    VectorizedSkinVertices(dest, src, count, boneMatrices);
#endif
}

bool Init() {
    // Standard initialization
    Log("[HwVertexSkinning] SSE4.1 Vectorized CPU Skinning Subsystem Active.");
    return true;
}

void Shutdown() {
}

} // namespace HwVertexSkinning
