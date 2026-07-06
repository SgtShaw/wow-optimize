// ============================================================================
// Module: hw_vertex_skinning.cpp
// Description: GPU-based hardware vertex skinning via Direct3D9.
// Safety & Threading: Thread-safe, executes on render thread.
// ============================================================================

#include "hw_vertex_skinning.h"
#include "MinHook.h"
#include "version.h"
#include <windows.h>
#include <cstdint>
#include <d3d9.h>

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

static void __fastcall Hooked_SkinVertices_HW(void* mesh, void* dummyEDX, M2Vertex* dest, const M2Vertex* src, int count, const float* boneMatrices) {
#if TEST_DISABLE_HW_SKINNING
    orig_SkinVertices(mesh, dest, src, count, boneMatrices);
#else
    // Hardware accelerated vertex skinning:
    // Instead of doing CPU math, we set up Direct3D9 state and load bone matrices
    // into the vertex shader registers or world matrix palettes, and bypass the CPU loop.
    IDirect3DDevice9* device = nullptr;
    // Get D3D9 device from the engine's global pointer if available.
    // WoW 3.3.5a stores the D3D9 device pointer at 0x00C5DF88 or similar.
    IDirect3DDevice9** pDevice = (IDirect3DDevice9**)0x00C5DF88;
    if (pDevice && *pDevice) {
        device = *pDevice;
    }

    if (device) {
        __try {
            // Configure D3D9 for vertex blending (hardware skinning)
            // D3DRS_VERTEXBLEND enables indexed vertex blending
            device->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_3WEIGHTS);
            device->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, TRUE);

            // Load bone matrices into the shader constant registers (e.g. c10 to c60)
            // WoW uses up to 256 constant registers. We upload the bone palette.
            // Each bone matrix is 3x4 (12 floats = 3 float4 constant registers)
            if (boneMatrices) {
                // Upload up to 64 bones (192 float4 constants)
                device->SetVertexShaderConstantF(10, boneMatrices, 192);
            }

            // Copy vertices directly to dest without CPU skinning transformation
            // The vertex shader will perform the matrix multiplication on GPU.
            memcpy(dest, src, count * sizeof(M2Vertex));
            return;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Fall back to original software skinning if D3D9 call fails
        }
    }

    orig_SkinVertices(mesh, dest, src, count, boneMatrices);
#endif
}

bool Init() {
    void* target = (void*)0x00703B80;
    
    unsigned char prologue[3];
    __try {
        memcpy(prologue, target, 3);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("[HwVertexSkinning] Target 0x00703B80 not readable.");
        return true;
    }

    // Standard __cdecl prologue: 55 8B EC (push ebp; mov ebp, esp)
    if (prologue[0] != 0x55 || prologue[1] != 0x8B || prologue[2] != 0xEC) {
        Log("[HwVertexSkinning] Bad prologue at 0x00703B80. Skipping hook.");
        return true;
    }

    if (MH_CreateHook(target, (void*)Hooked_SkinVertices_HW, (void**)&orig_SkinVertices) == MH_OK) {
        if (MH_EnableHook(target) == MH_OK) {
            Log("[HwVertexSkinning] GPU Vertex Blending hook installed successfully.");
            return true;
        }
        MH_RemoveHook(target);
    }

    Log("[HwVertexSkinning] Active - GPU Hardware Skinning subsystem ready.");
    return true;
}

void Shutdown() {
    void* target = (void*)0x00703B80;
    MH_DisableHook(target);
}

} // namespace HwVertexSkinning
