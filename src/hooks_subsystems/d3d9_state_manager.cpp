// ============================================================================
// Module: d3d9_state_manager.cpp
// Description: Deduplicates D3D9 device state changes and batches draw calls
//              using Hardware Geometry Instancing for massive CPU performance.
// Safety & Threading: Main render thread only.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <d3d9.h>
#include "d3d9_state_manager.h"

extern "C" void Log(const char* fmt, ...);

// ================================================================
// Memory validation
// ================================================================
static bool IsReadable(uintptr_t addr) {
    if (addr == 0) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((void*)addr, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    return !(mbi.Protect & PAGE_NOACCESS) && !(mbi.Protect & PAGE_GUARD);
}

// ================================================================
// VTable indices (IDirect3DDevice9)
// ================================================================
enum {
    V_SETSAMPLERSTATE      = 69,
    V_SETTEXTURESTAGESTATE = 64,
    V_SETRENDERSTATE       = 57,
    V_SETTRANSFORM         = 44,
    V_SETMATERIAL          = 33,
    V_SETVIEWPORT          = 47,
    V_SETSCISSORRECT       = 67,
    V_SETSTREAMSOURCE      = 36,
    V_SETINDICES           = 37,
    V_SETVERTEXDECLARATION = 87,
    V_SETFVF               = 89,
    V_SETPIXELSHADER       = 93,
    V_SETVERTEXSHADER      = 91,
    V_SETTEXTURE           = 65,
    V_DRAWINDEXEDPRIMITIVE = 82,
    V_DRAWPRIMITIVE        = 81,
};

static constexpr int NUM_HOOKS = 15;
static int g_vtableIndices[NUM_HOOKS] = {
    V_SETRENDERSTATE, V_SETTEXTURESTAGESTATE, V_SETSAMPLERSTATE,
    V_SETTEXTURE, V_SETTRANSFORM, V_SETMATERIAL,
    V_SETVIEWPORT, V_SETSCISSORRECT, V_SETSTREAMSOURCE,
    V_SETINDICES, V_SETVERTEXDECLARATION, V_SETFVF,
    V_SETVERTEXSHADER, V_SETPIXELSHADER, V_SETRENDERSTATE
};

static void* g_vtableOriginals[NUM_HOOKS] = {};
static bool  g_vtablePatched[NUM_HOOKS] = {};

static void* g_pDevice = nullptr;
static bool  g_deviceHooked = false;

// ================================================================
// Per-frame statistics
// ================================================================
static volatile LONG64 g_statCalls[NUM_HOOKS]   = {};
static volatile LONG64 g_statSkipped[NUM_HOOKS] = {};
static const char* g_statNames[NUM_HOOKS] = {
    "SetRenderState", "SetTextureStageState", "SetSamplerState",
    "SetTexture", "SetTransform", "SetMaterial",
    "SetViewport", "SetScissorRect", "SetStreamSource",
    "SetIndices", "SetVertexDeclaration", "SetFVF",
    "SetVertexShader", "SetPixelShader", "RenderTarget"
};

static volatile LONG64 g_drawCalls = 0;
static volatile LONG64 g_totalFrames = 0;

// ================================================================
// State caches
// ================================================================
static DWORD  g_rsCache[256] = {};
static bool   g_rsValid[256] = {};
static DWORD  g_tssCache[256] = {};
static bool   g_tssValid[256] = {};
static DWORD  g_ssCache[256] = {};
static bool   g_ssValid[256] = {};
static void*  g_texCache[8] = {};
static bool   g_texValid[8] = {};
static uint64_t g_xformHash[32] = {};
static bool   g_xformValid[32] = {};
static uint32_t g_materialHash = 0;
static bool   g_materialValid = false;
static DWORD    g_viewportData[6] = {};
static LONG     g_scissorData[4] = {};
static bool     g_scissorValid = false;
static void*  g_streamBuf[16] = {};
static bool   g_streamValid[16] = {};
static void*  g_indexBuf = nullptr;
static bool   g_indexValid = false;
static void*  g_vertDecl = nullptr;
static bool   g_vertDeclValid = false;
static DWORD  g_fvf = 0;
static bool   g_fvfValid = false;
static void*  g_vs = nullptr;
static bool   g_vsValid = false;
static void*  g_ps = nullptr;
static bool   g_psValid = false;

// ================================================================
// Hardware Instancing Batcher Structures
// ================================================================
struct BatchedDraw {
    D3DPRIMITIVETYPE type;
    INT baseVertex;
    UINT minIndex;
    UINT numVertices;
    UINT startIndex;
    UINT primCount;
    float worldMatrix[16];
};

static constexpr int MAX_BATCH_SIZE = 128;
static BatchedDraw g_batchQueue[MAX_BATCH_SIZE];
static int g_batchCount = 0;

static void* g_lastDevice = nullptr;
static void* g_lastVB = nullptr;
static void* g_lastIB = nullptr;
static void* g_lastDecl = nullptr;
static void* g_lastTex = nullptr;
static void* g_lastVS = nullptr;
static void* g_lastPS = nullptr;
static DWORD g_lastFVF = 0;
static DWORD g_lastType = 0;
static UINT g_lastStride = 32;

static UINT g_streamStride[16] = {};

static float g_activeWorldMatrix[16] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
};

// Batcher Diagnostic Counters
static volatile LONG64 g_batchedDrawsCount = 0;
static volatile LONG64 g_batchFlushesCount = 0;
static volatile LONG64 g_batchInstancedDrawsCount = 0;

// ================================================================
// Fast matrix hash for SetTransform dedup
// ================================================================
static uint64_t QuickMatrixHash(const float* m) {
    uint64_t h = 0;
    const uint32_t* p = (const uint32_t*)m;
    for (int i = 0; i < 12; i++) {
        h ^= (uint64_t)p[i] << (i % 32);
        h = (h * 0x9E3779B97F4A7C15ULL) ^ (h >> 31);
    }
    return h;
}

static uint32_t HashMaterial(const DWORD* mat) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 16; i++) {
        h ^= mat[i];
        h *= 16777619u;
    }
    return h;
}

// ================================================================
// original function pointers for calling back to driver
// ================================================================
typedef HRESULT (__stdcall *SetRenderState_t)(void* dev, DWORD state, DWORD value);
static SetRenderState_t g_orig_SetRenderState = nullptr;

typedef HRESULT (__stdcall *SetTextureStageState_t)(void* dev, DWORD stage, DWORD type, DWORD value);
static SetTextureStageState_t g_orig_SetTextureStageState = nullptr;

typedef HRESULT (__stdcall *SetSamplerState_t)(void* dev, DWORD sampler, DWORD type, DWORD value);
static SetSamplerState_t g_orig_SetSamplerState = nullptr;

typedef HRESULT (__stdcall *SetTexture_t)(void* dev, DWORD stage, void* tex);
static SetTexture_t g_orig_SetTexture = nullptr;

typedef HRESULT (__stdcall *SetTransform_t)(void* dev, DWORD state, const void* matrix);
static SetTransform_t g_orig_SetTransform = nullptr;

typedef HRESULT (__stdcall *SetMaterial_t)(void* dev, const void* material);
static SetMaterial_t g_orig_SetMaterial = nullptr;

typedef HRESULT (__stdcall *SetViewport_t)(void* dev, const DWORD* vp);
static SetViewport_t g_orig_SetViewport = nullptr;

typedef HRESULT (__stdcall *SetScissorRect_t)(void* dev, const RECT* rect);
static SetScissorRect_t g_orig_SetScissorRect = nullptr;

typedef HRESULT (__stdcall *SetStreamSource_t)(void* dev, UINT stream, void* vb, UINT offset, UINT stride);
static SetStreamSource_t g_orig_SetStreamSource = nullptr;

typedef HRESULT (__stdcall *SetIndices_t)(void* dev, void* ib);
static SetIndices_t g_orig_SetIndices = nullptr;

typedef HRESULT (__stdcall *SetVertexDeclaration_t)(void* dev, void* decl);
static SetVertexDeclaration_t g_orig_SetVertexDeclaration = nullptr;

typedef HRESULT (__stdcall *SetFVF_t)(void* dev, DWORD fvf);
static SetFVF_t g_orig_SetFVF = nullptr;

typedef HRESULT (__stdcall *SetVertexShader_t)(void* dev, void* vs);
static SetVertexShader_t g_orig_SetVertexShader = nullptr;

typedef HRESULT (__stdcall *SetPixelShader_t)(void* dev, void* ps);
static SetPixelShader_t g_orig_SetPixelShader = nullptr;

typedef HRESULT (__stdcall *DrawIndexedPrimitive_t)(void* dev, DWORD type, INT baseVertex, UINT minIndex, UINT numVertices, UINT startIndex, UINT primCount);
static DrawIndexedPrimitive_t g_orig_DrawIndexedPrimitive = nullptr;

// ================================================================
// Batcher Flush Pipeline
// ================================================================
static void FlushBatch(void* dev) {
    if (g_batchCount == 0) return;
    
    IDirect3DDevice9* device = (IDirect3DDevice9*)dev;
    InterlockedIncrement64(&g_batchFlushesCount);
    
    if (g_batchCount == 1) {
        const auto& draw = g_batchQueue[0];
        g_orig_SetTransform(device, D3DTS_WORLD, (const D3DMATRIX*)draw.worldMatrix);
        g_orig_DrawIndexedPrimitive(device, draw.type, draw.baseVertex, draw.minIndex, draw.numVertices, draw.startIndex, draw.primCount);
    } else {
        // Use Hardware Instancing for Fixed-Function Pipeline (No custom VS)
        if (!g_vs && g_streamBuf[0] && g_lastStride > 0 && g_batchQueue[0].numVertices < 1000) {
            static IDirect3DVertexBuffer9* s_instanceVB = nullptr;
            static void* s_lastDeviceForVB = nullptr;
            
            if (s_lastDeviceForVB != dev) {
                if (s_instanceVB) {
                    s_instanceVB->Release();
                    s_instanceVB = nullptr;
                }
                s_lastDeviceForVB = dev;
            }
            
            if (!s_instanceVB) {
                HRESULT hr = device->CreateVertexBuffer(MAX_BATCH_SIZE * sizeof(float) * 16, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &s_instanceVB, NULL);
                if (FAILED(hr)) {
                    s_instanceVB = nullptr;
                }
            }
            
            if (s_instanceVB) {
                void* pData = nullptr;
                if (SUCCEEDED(s_instanceVB->Lock(0, g_batchCount * sizeof(float) * 16, &pData, D3DLOCK_DISCARD))) {
                    float* pFloats = (float*)pData;
                    for (int i = 0; i < g_batchCount; ++i) {
                        memcpy(pFloats + i * 16, g_batchQueue[i].worldMatrix, sizeof(float) * 16);
                    }
                    s_instanceVB->Unlock();
                }
                
                // Set stream frequency for instanced rendering
                g_orig_SetStreamSource(device, 0, g_streamBuf[0], 0, g_lastStride);
                device->SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | g_batchCount);
                
                g_orig_SetStreamSource(device, 1, s_instanceVB, 0, sizeof(float) * 16);
                device->SetStreamSourceFreq(1, D3DSTREAMSOURCE_INSTANCEDATA | 1);
                
                const auto& draw = g_batchQueue[0];
                g_orig_DrawIndexedPrimitive(device, draw.type, draw.baseVertex, draw.minIndex, draw.numVertices, draw.startIndex, draw.primCount);
                
                device->SetStreamSourceFreq(0, 1);
                device->SetStreamSourceFreq(1, 1);
                
                InterlockedIncrement64(&g_batchInstancedDrawsCount);
            } else {
                for (int i = 0; i < g_batchCount; ++i) {
                    const auto& draw = g_batchQueue[i];
                    g_orig_SetTransform(device, D3DTS_WORLD, (const D3DMATRIX*)draw.worldMatrix);
                    g_orig_DrawIndexedPrimitive(device, draw.type, draw.baseVertex, draw.minIndex, draw.numVertices, draw.startIndex, draw.primCount);
                }
            }
        } else {
            // Fallback: draw sequentially
            for (int i = 0; i < g_batchCount; ++i) {
                const auto& draw = g_batchQueue[i];
                g_orig_SetTransform(device, D3DTS_WORLD, (const D3DMATRIX*)draw.worldMatrix);
                g_orig_DrawIndexedPrimitive(device, draw.type, draw.baseVertex, draw.minIndex, draw.numVertices, draw.startIndex, draw.primCount);
            }
        }
    }
    
    g_batchCount = 0;
}

// ================================================================
// Hooked functions
// ================================================================

static HRESULT __stdcall Hooked_SetRenderState(void* dev, DWORD state, DWORD value) {
    InterlockedIncrement64(&g_statCalls[0]);
    FlushBatch(dev);
    
    if (state < 256 && g_rsValid[state] && g_rsCache[state] == value) {
        InterlockedIncrement64(&g_statSkipped[0]);
        return 0;
    }
    HRESULT hr = g_orig_SetRenderState(dev, state, value);
    if (SUCCEEDED(hr) && state < 256) {
        g_rsCache[state] = value;
        g_rsValid[state] = true;
    }
    return hr;
}

static HRESULT __stdcall Hooked_SetTextureStageState(void* dev, DWORD stage, DWORD type, DWORD value) {
    InterlockedIncrement64(&g_statCalls[1]);
    FlushBatch(dev);

    DWORD idx = (stage & 7) * 32 + (type & 31);
    if (idx < 256 && g_tssValid[idx] && g_tssCache[idx] == value) {
        InterlockedIncrement64(&g_statSkipped[1]);
        return 0;
    }
    HRESULT hr = g_orig_SetTextureStageState(dev, stage, type, value);
    if (SUCCEEDED(hr) && idx < 256) {
        g_tssCache[idx] = value;
        g_tssValid[idx] = true;
    }
    return hr;
}

static HRESULT __stdcall Hooked_SetSamplerState(void* dev, DWORD sampler, DWORD type, DWORD value) {
    InterlockedIncrement64(&g_statCalls[2]);
    FlushBatch(dev);

    DWORD idx = (sampler & 15) * 16 + (type & 15);
    if (idx < 256 && g_ssValid[idx] && g_ssCache[idx] == value) {
        InterlockedIncrement64(&g_statSkipped[2]);
        return 0;
    }
    HRESULT hr = g_orig_SetSamplerState(dev, sampler, type, value);
    if (SUCCEEDED(hr) && idx < 256) {
        g_ssCache[idx] = value;
        g_ssValid[idx] = true;
    }
    return hr;
}

static HRESULT __stdcall Hooked_SetTexture(void* dev, DWORD stage, void* tex) {
    InterlockedIncrement64(&g_statCalls[3]);
    FlushBatch(dev);

    if (stage < 8 && g_texValid[stage] && g_texCache[stage] == tex) {
        InterlockedIncrement64(&g_statSkipped[3]);
        return 0;
    }
    HRESULT hr = g_orig_SetTexture(dev, stage, tex);
    if (SUCCEEDED(hr) && stage < 8) {
        g_texCache[stage] = tex;
        g_texValid[stage] = true;
    }
    return hr;
}

static HRESULT __stdcall Hooked_SetTransform(void* dev, DWORD state, const void* matrix) {
    InterlockedIncrement64(&g_statCalls[4]);
    
    if (state == D3DTS_WORLD && matrix) {
        memcpy(g_activeWorldMatrix, matrix, sizeof(float) * 16);
    }
    
    FlushBatch(dev);

    DWORD idx = state;
    if (idx < 32) {
        uint64_t hash = QuickMatrixHash((const float*)matrix);
        if (g_xformValid[idx] && g_xformHash[idx] == hash) {
            InterlockedIncrement64(&g_statSkipped[4]);
            return 0;
        }
        HRESULT hr = g_orig_SetTransform(dev, state, matrix);
        if (SUCCEEDED(hr)) {
            g_xformHash[idx] = hash;
            g_xformValid[idx] = true;
        }
        return hr;
    }
    return g_orig_SetTransform(dev, state, matrix);
}

static HRESULT __stdcall Hooked_SetMaterial(void* dev, const void* material) {
    InterlockedIncrement64(&g_statCalls[5]);
    FlushBatch(dev);

    uint32_t hash = HashMaterial((const DWORD*)material);
    if (g_materialValid && g_materialHash == hash) {
        InterlockedIncrement64(&g_statSkipped[5]);
        return 0;
    }
    HRESULT hr = g_orig_SetMaterial(dev, material);
    if (SUCCEEDED(hr)) {
        g_materialHash = hash;
        g_materialValid = true;
    }
    return hr;
}

static HRESULT __stdcall Hooked_SetViewport(void* dev, const DWORD* vp) {
    InterlockedIncrement64(&g_statCalls[6]);
    FlushBatch(dev);

    if (memcmp(g_viewportData, vp, sizeof(g_viewportData)) == 0) {
        InterlockedIncrement64(&g_statSkipped[6]);
        return 0;
    }
    HRESULT hr = g_orig_SetViewport(dev, vp);
    if (SUCCEEDED(hr)) {
        memcpy(g_viewportData, vp, sizeof(g_viewportData));
    }
    return hr;
}

static HRESULT __stdcall Hooked_SetScissorRect(void* dev, const RECT* rect) {
    InterlockedIncrement64(&g_statCalls[7]);
    FlushBatch(dev);

    if (g_scissorValid
        && g_scissorData[0] == rect->left
        && g_scissorData[1] == rect->top
        && g_scissorData[2] == rect->right
        && g_scissorData[3] == rect->bottom) {
        InterlockedIncrement64(&g_statSkipped[7]);
        return 0;
    }
    HRESULT hr = g_orig_SetScissorRect(dev, rect);
    if (SUCCEEDED(hr)) {
        g_scissorData[0] = rect->left;
        g_scissorData[1] = rect->top;
        g_scissorData[2] = rect->right;
        g_scissorData[3] = rect->bottom;
        g_scissorValid = true;
    }
    return hr;
}

static HRESULT __stdcall Hooked_SetStreamSource(void* dev, UINT stream, void* vb, UINT offset, UINT stride) {
    InterlockedIncrement64(&g_statCalls[8]);
    
    if (stream < 16) {
        g_streamStride[stream] = stride;
    }
    
    FlushBatch(dev);

    if (stream < 16 && g_streamValid[stream] && g_streamBuf[stream] == vb) {
        InterlockedIncrement64(&g_statSkipped[8]);
        return 0;
    }
    HRESULT hr = g_orig_SetStreamSource(dev, stream, vb, offset, stride);
    if (SUCCEEDED(hr) && stream < 16) {
        g_streamBuf[stream] = vb;
        g_streamValid[stream] = true;
    }
    return hr;
}

static HRESULT __stdcall Hooked_SetIndices(void* dev, void* ib) {
    InterlockedIncrement64(&g_statCalls[9]);
    FlushBatch(dev);

    if (g_indexValid && g_indexBuf == ib) {
        InterlockedIncrement64(&g_statSkipped[9]);
        return 0;
    }
    HRESULT hr = g_orig_SetIndices(dev, ib);
    if (SUCCEEDED(hr)) {
        g_indexBuf = ib;
        g_indexValid = true;
    }
    return hr;
}

static HRESULT __stdcall Hooked_SetVertexDeclaration(void* dev, void* decl) {
    InterlockedIncrement64(&g_statCalls[10]);
    FlushBatch(dev);

    if (g_vertDeclValid && g_vertDecl == decl) {
        InterlockedIncrement64(&g_statSkipped[10]);
        return 0;
    }
    HRESULT hr = g_orig_SetVertexDeclaration(dev, decl);
    if (SUCCEEDED(hr)) {
        g_vertDecl = decl;
        g_vertDeclValid = true;
    }
    return hr;
}

static HRESULT __stdcall Hooked_SetFVF(void* dev, DWORD fvf) {
    InterlockedIncrement64(&g_statCalls[11]);
    FlushBatch(dev);

    if (g_fvfValid && g_fvf == fvf) {
        InterlockedIncrement64(&g_statSkipped[11]);
        return 0;
    }
    HRESULT hr = g_orig_SetFVF(dev, fvf);
    if (SUCCEEDED(hr)) {
        g_fvf = fvf;
        g_fvfValid = true;
    }
    return hr;
}

static HRESULT __stdcall Hooked_SetVertexShader(void* dev, void* vs) {
    InterlockedIncrement64(&g_statCalls[12]);
    FlushBatch(dev);

    if (g_vsValid && g_vs == vs) {
        InterlockedIncrement64(&g_statSkipped[12]);
        return 0;
    }
    HRESULT hr = g_orig_SetVertexShader(dev, vs);
    if (SUCCEEDED(hr)) {
        g_vs = vs;
        g_vsValid = true;
    }
    return hr;
}

static HRESULT __stdcall Hooked_SetPixelShader(void* dev, void* ps) {
    InterlockedIncrement64(&g_statCalls[13]);
    FlushBatch(dev);

    if (g_psValid && g_ps == ps) {
        InterlockedIncrement64(&g_statSkipped[13]);
        return 0;
    }
    HRESULT hr = g_orig_SetPixelShader(dev, ps);
    if (SUCCEEDED(hr)) {
        g_ps = ps;
        g_psValid = true;
    }
    return hr;
}

static HRESULT __stdcall Hooked_DrawIndexedPrimitive(void* dev, DWORD type, INT baseVertex, UINT minIndex, UINT numVertices, UINT startIndex, UINT primCount) {
    InterlockedIncrement64(&g_drawCalls);
    InterlockedIncrement64(&g_batchedDrawsCount);

    void* vb = g_streamBuf[0];
    void* ib = g_indexBuf;
    void* decl = g_vertDecl;
    void* tex = g_texCache[0];
    void* vs = g_vs;
    void* ps = g_ps;
    DWORD fvf = g_fvf;
    UINT stride = g_streamStride[0];

    if (g_batchCount > 0) {
        if (g_lastDevice == dev && g_lastVB == vb && g_lastIB == ib && g_lastDecl == decl &&
            g_lastTex == tex && g_lastVS == vs && g_lastPS == ps && g_lastFVF == fvf &&
            g_lastType == type && g_lastStride == stride && g_batchCount < MAX_BATCH_SIZE) {
            
            auto& draw = g_batchQueue[g_batchCount++];
            draw.type = (D3DPRIMITIVETYPE)type;
            draw.baseVertex = baseVertex;
            draw.minIndex = minIndex;
            draw.numVertices = numVertices;
            draw.startIndex = startIndex;
            draw.primCount = primCount;
            memcpy(draw.worldMatrix, g_activeWorldMatrix, sizeof(g_activeWorldMatrix));
            
            return D3D_OK;
        } else {
            FlushBatch(dev);
        }
    }

    g_lastDevice = dev;
    g_lastVB = vb;
    g_lastIB = ib;
    g_lastDecl = decl;
    g_lastTex = tex;
    g_lastVS = vs;
    g_lastPS = ps;
    g_lastFVF = fvf;
    g_lastType = type;
    g_lastStride = (stride > 0) ? stride : 32;

    auto& draw = g_batchQueue[g_batchCount++];
    draw.type = (D3DPRIMITIVETYPE)type;
    draw.baseVertex = baseVertex;
    draw.minIndex = minIndex;
    draw.numVertices = numVertices;
    draw.startIndex = startIndex;
    draw.primCount = primCount;
    memcpy(draw.worldMatrix, g_activeWorldMatrix, sizeof(g_activeWorldMatrix));

    return D3D_OK;
}

// ================================================================
// VTable patching
// ================================================================
static void* g_hookFuncs[NUM_HOOKS] = {
    (void*)Hooked_SetRenderState,
    (void*)Hooked_SetTextureStageState,
    (void*)Hooked_SetSamplerState,
    (void*)Hooked_SetTexture,
    (void*)Hooked_SetTransform,
    (void*)Hooked_SetMaterial,
    (void*)Hooked_SetViewport,
    (void*)Hooked_SetScissorRect,
    (void*)Hooked_SetStreamSource,
    (void*)Hooked_SetIndices,
    (void*)Hooked_SetVertexDeclaration,
    (void*)Hooked_SetFVF,
    (void*)Hooked_SetVertexShader,
    (void*)Hooked_SetPixelShader,
    nullptr
};

static void SetHookOrigin(int idx, void* orig) {
    switch (idx) {
        case 0:  g_orig_SetRenderState       = (SetRenderState_t)orig; break;
        case 1:  g_orig_SetTextureStageState = (SetTextureStageState_t)orig; break;
        case 2:  g_orig_SetSamplerState      = (SetSamplerState_t)orig; break;
        case 3:  g_orig_SetTexture            = (SetTexture_t)orig; break;
        case 4:  g_orig_SetTransform          = (SetTransform_t)orig; break;
        case 5:  g_orig_SetMaterial           = (SetMaterial_t)orig; break;
        case 6:  g_orig_SetViewport           = (SetViewport_t)orig; break;
        case 7:  g_orig_SetScissorRect        = (SetScissorRect_t)orig; break;
        case 8:  g_orig_SetStreamSource       = (SetStreamSource_t)orig; break;
        case 9:  g_orig_SetIndices            = (SetIndices_t)orig; break;
        case 10: g_orig_SetVertexDeclaration  = (SetVertexDeclaration_t)orig; break;
        case 11: g_orig_SetFVF                = (SetFVF_t)orig; break;
        case 12: g_orig_SetVertexShader       = (SetVertexShader_t)orig; break;
        case 13: g_orig_SetPixelShader        = (SetPixelShader_t)orig; break;
        default: break;
    }
}

#ifndef ADDR_CGXDEVICED3D_PTR
#define ADDR_CGXDEVICED3D_PTR  0x00C5DF88  // global CGxDeviceD3d*
#endif

static bool PatchDeviceVTable(void* pDevice) {
    if (!pDevice || g_deviceHooked) return false;

    uintptr_t* vtable = *(uintptr_t**)pDevice;
    if (!vtable) return false;

    int patched = 0;
    for (int i = 0; i < NUM_HOOKS; i++) {
        int vtIndex = g_vtableIndices[i];
        if (!g_hookFuncs[i]) continue;

        uintptr_t origFunc = vtable[vtIndex];
        if (!IsReadable(origFunc)) {
            Log("[D3D9State] Skipping vtable[%d] — original not readable", vtIndex);
            continue;
        }

        DWORD oldProtect;
        if (!VirtualProtect(&vtable[vtIndex], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            Log("[D3D9State] VirtualProtect failed for vtable[%d]", vtIndex);
            for (int j = i - 1; j >= 0; j--) {
                if (g_vtablePatched[j]) {
                    vtable[g_vtableIndices[j]] = (uintptr_t)g_vtableOriginals[j];
                    g_vtablePatched[j] = false;
                }
            }
            return false;
        }

        g_vtableOriginals[i] = (void*)origFunc;
        SetHookOrigin(i, (void*)origFunc);
        vtable[vtIndex] = (uintptr_t)g_hookFuncs[i];
        VirtualProtect(&vtable[vtIndex], sizeof(void*), oldProtect, &oldProtect);
        g_vtablePatched[i] = true;
        patched++;
    }

    // Patch DrawIndexedPrimitive
    {
        int vtIdxDraw = V_DRAWINDEXEDPRIMITIVE;
        uintptr_t origDraw = vtable[vtIdxDraw];
        if (IsReadable(origDraw)) {
            DWORD oldProtect;
            if (VirtualProtect(&vtable[vtIdxDraw], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                g_orig_DrawIndexedPrimitive = (DrawIndexedPrimitive_t)origDraw;
                vtable[vtIdxDraw] = (uintptr_t)Hooked_DrawIndexedPrimitive;
                VirtualProtect(&vtable[vtIdxDraw], sizeof(void*), oldProtect, &oldProtect);
            }
        }
    }

    g_pDevice = pDevice;
    g_deviceHooked = true;
    Log("[D3D9State] Device vtable patched: %d/%d state hooks installed", patched, NUM_HOOKS);
    return true;
}

static void UnpatchDeviceVTable() {
    if (!g_pDevice || !g_deviceHooked) return;

    uintptr_t* vtable = *(uintptr_t**)g_pDevice;
    if (!vtable) { g_deviceHooked = false; g_pDevice = nullptr; return; }

    // Restore DrawIndexedPrimitive
    if (g_orig_DrawIndexedPrimitive) {
        DWORD oldProtect;
        if (VirtualProtect(&vtable[V_DRAWINDEXEDPRIMITIVE], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            vtable[V_DRAWINDEXEDPRIMITIVE] = (uintptr_t)g_orig_DrawIndexedPrimitive;
            VirtualProtect(&vtable[V_DRAWINDEXEDPRIMITIVE], sizeof(void*), oldProtect, &oldProtect);
        }
    }

    // Restore state hooks in reverse order
    for (int i = NUM_HOOKS - 1; i >= 0; i--) {
        if (!g_vtablePatched[i]) continue;
        int vtIndex = g_vtableIndices[i];
        DWORD oldProtect;
        if (VirtualProtect(&vtable[vtIndex], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            vtable[vtIndex] = (uintptr_t)g_vtableOriginals[i];
            VirtualProtect(&vtable[vtIndex], sizeof(void*), oldProtect, &oldProtect);
        }
        g_vtablePatched[i] = false;
    }

    g_deviceHooked = false;
    g_pDevice = nullptr;
}

static void InvalidateAllCaches() {
    memset(g_rsValid, 0, sizeof(g_rsValid));
    memset(g_tssValid, 0, sizeof(g_tssValid));
    memset(g_ssValid, 0, sizeof(g_ssValid));
    memset(g_texValid, 0, sizeof(g_texValid));
    memset(g_xformValid, 0, sizeof(g_xformValid));
    g_materialValid = false;
    g_scissorValid = false;
    memset(g_streamValid, 0, sizeof(g_streamValid));
    g_indexValid = false;
    g_vertDeclValid = false;
    g_fvfValid = false;
    g_vsValid = false;
    g_psValid = false;
}

static bool TryFindAndPatchDevice() {
    if (g_deviceHooked) return true;

    uintptr_t addr = ADDR_CGXDEVICED3D_PTR;
    if (addr == 0 || !IsReadable(addr)) return false;
    uintptr_t pGxDevice = *(uintptr_t*)addr;
    if (!pGxDevice || !IsReadable(pGxDevice)) return false;

    uintptr_t devicePtrAddr = pGxDevice + 0x397C;
    if (!IsReadable(devicePtrAddr)) return false;
    void* pDevice = *(void**)devicePtrAddr;
    if (!pDevice || !IsReadable((uintptr_t)pDevice)) return false;

    uintptr_t* vtable = *(uintptr_t**)pDevice;
    if (!vtable || !IsReadable((uintptr_t)vtable)) return false;

    return PatchDeviceVTable(pDevice);
}

// ================================================================
// Public API
// ================================================================
bool IsD3D9DeviceHooked(void) { return g_deviceHooked; }

bool InstallD3D9StateManager(void) {
    memset(g_rsCache, 0, sizeof(g_rsCache));
    memset(g_rsValid, 0, sizeof(g_rsValid));
    memset(g_tssCache, 0, sizeof(g_tssCache));
    memset(g_tssValid, 0, sizeof(g_tssValid));
    memset(g_ssCache, 0, sizeof(g_ssCache));
    memset(g_ssValid, 0, sizeof(g_ssValid));
    memset(g_texCache, 0, sizeof(g_texCache));
    memset(g_texValid, 0, sizeof(g_texValid));
    memset(g_xformHash, 0, sizeof(g_xformHash));
    memset(g_xformValid, 0, sizeof(g_xformValid));
    memset(g_streamBuf, 0, sizeof(g_streamBuf));
    memset(g_streamValid, 0, sizeof(g_streamValid));
    memset((void*)g_statCalls, 0, sizeof(g_statCalls));
    memset((void*)g_statSkipped, 0, sizeof(g_statSkipped));
    g_drawCalls = 0;
    g_totalFrames = 0;
    g_batchCount = 0;

    bool ok = TryFindAndPatchDevice();
    if (ok) {
        Log("[D3D9State] [ OK ] Device vtable patched (%d hooks)", NUM_HOOKS);
    } else {
        Log("[D3D9State] Device not found at init — retrying each frame");
    }

    return true;
}

void ShutdownD3D9StateManager(void) {
    if (g_pDevice) FlushBatch(g_pDevice);
    UnpatchDeviceVTable();

    Log("[D3D9State] ===== Final Statistics (%lld frames) =====", g_totalFrames);
    if (g_totalFrames > 0) {
        Log("[D3D9State] Draw calls: %lld (%.0f/frame)",
            g_drawCalls, (double)g_drawCalls / g_totalFrames);
        Log("[D3D9State] Instancer stats: %lld draws batched, %lld flushes, %lld instanced draws",
            g_batchedDrawsCount, g_batchFlushesCount, g_batchInstancedDrawsCount);
    }
}

void OnFrameD3D9StateManager(DWORD mainThreadId) {
    if (GetCurrentThreadId() != mainThreadId) return;

    g_totalFrames++;
    
    if (g_pDevice) {
        FlushBatch(g_pDevice);
    }

    if (!g_deviceHooked) {
        TryFindAndPatchDevice();
    }
}
