// ============================================================================
// Module: d3d9_state_cache.cpp
// Description: Filters redundant D3D9 state modifications to reduce context switches.
// Safety & Threading: Main thread only. Invalidates cache on Reset().
// ============================================================================

#include "d3d9_state_cache.h"
#include "MinHook.h"
#include "version.h"
#include "mip_bias_governor.h"
#include <d3d9.h>
#include "d3d9_render_thread.h"
#include "config.h"
#include <atomic>

extern "C" void Log(const char* fmt, ...);
extern DWORD g_mainThreadId;

namespace D3D9StateCache {

// Original function pointers


typedef HRESULT (WINAPI *SetRenderState_fn)(IDirect3DDevice9* device, D3DRENDERSTATETYPE state, DWORD value);
SetRenderState_fn orig_SetRenderState = nullptr;

typedef HRESULT (WINAPI *SetTransform_fn)(IDirect3DDevice9* device, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix);
SetTransform_fn orig_SetTransform = nullptr;

typedef HRESULT (WINAPI *SetViewport_fn)(IDirect3DDevice9* device, const D3DVIEWPORT9* viewport);
SetViewport_fn orig_SetViewport = nullptr;

typedef HRESULT (WINAPI *CreateVertexBuffer_fn)(IDirect3DDevice9* device, UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle);
static CreateVertexBuffer_fn orig_CreateVertexBuffer = nullptr;

typedef HRESULT (WINAPI *VB_Lock_fn)(IDirect3DVertexBuffer9* vb, UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags);
static VB_Lock_fn orig_VB_Lock = nullptr;

typedef HRESULT (WINAPI *VB_Unlock_fn)(IDirect3DVertexBuffer9* vb);
static VB_Unlock_fn orig_VB_Unlock = nullptr;

typedef HRESULT (WINAPI *SetVertexShaderConstantF_fn)(IDirect3DDevice9* device, UINT StartRegister, const float* pConstantData, UINT Vector4fCount);
SetVertexShaderConstantF_fn orig_SetVertexShaderConstantF = nullptr;

typedef HRESULT (WINAPI *SetSamplerState_fn)(IDirect3DDevice9* device, DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value);
SetSamplerState_fn orig_SetSamplerState = nullptr;

typedef HRESULT (WINAPI *SetTextureStageState_fn)(IDirect3DDevice9* device, DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value);
SetTextureStageState_fn orig_SetTextureStageState = nullptr;

static bool g_vbHooksInstalled = false;



typedef HRESULT (WINAPI *Reset_fn)(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params);
Reset_fn orig_Reset = nullptr;

typedef HRESULT (WINAPI *Present_fn)(IDirect3DDevice9* device, const RECT* src, const RECT* dest, HWND window, const RGNDATA* dirty);
Present_fn orig_Present = nullptr;

// Cache structures
static IDirect3DBaseTexture9* g_textureCache[16] = { nullptr };

static DWORD g_renderStateCache[512] = { 0 };
static bool g_renderStateValid[512] = { false };

static DWORD g_textureStageStateCache[8][64] = { {0} };
static bool g_textureStageStateValid[8][64] = { {false} };

static DWORD g_samplerStateCache[16][32] = { {0} };
static bool g_samplerStateValid[16][32] = { {false} };

struct CachedMatrix {
    D3DMATRIX matrix;
    bool valid;
};
static CachedMatrix g_transformCache[512] = { { {0}, false } };

static D3DVIEWPORT9 g_viewportCache = { 0 };
static bool g_viewportValid = false;

struct ShadowBufferEntry {
    IDirect3DVertexBuffer9* vb;
    void* data;
    UINT size;
    bool valid;
};
static constexpr int VB_CACHE_SIZE = 128;
static constexpr int VB_CACHE_MASK = VB_CACHE_SIZE - 1;
static ShadowBufferEntry g_vbCache[VB_CACHE_SIZE] = {};

static inline unsigned int HashVB(IDirect3DVertexBuffer9* vb) {
    uintptr_t val = (uintptr_t)vb;
    return (uint32_t)((val ^ (val >> 12)) & VB_CACHE_MASK);
}

struct ConstantRegister {
    float val[4];
    bool valid;
};
static ConstantRegister g_vsConstantCache[256] = { { {0.0f}, false } };

// Latency reduction structures (Max Frame Latency = 1)
#define LATENCY_QUEUE_SIZE 2
static IDirect3DQuery9* g_latencyQueries[LATENCY_QUEUE_SIZE] = { nullptr };
static int g_latencyQueryIndex = 0;
static bool g_latencyInitialized = false;

static void InvalidateLatencyQueries() {
    for (int i = 0; i < LATENCY_QUEUE_SIZE; i++) {
        if (g_latencyQueries[i]) {
            g_latencyQueries[i]->Release();
            g_latencyQueries[i] = nullptr;
        }
    }
    g_latencyInitialized = false;
    g_latencyQueryIndex = 0;
}

// Statistics
static std::atomic<long> g_textureSkips{0};
static std::atomic<long> g_renderStateSkips{0};
static std::atomic<long> g_stageStateSkips{0};
static std::atomic<long> g_samplerSkips{0};
static std::atomic<long> g_transformSkips{0};
static std::atomic<long> g_viewportSkips{0};
static std::atomic<long> g_vsConstantSkips{0};

// Clear the cache (called on Init and after device Reset)
static void CleanVBCache() {
    for (int i = 0; i < VB_CACHE_SIZE; i++) {
        if (g_vbCache[i].valid && g_vbCache[i].data) {
            _aligned_free(g_vbCache[i].data);
            g_vbCache[i].data = nullptr;
            g_vbCache[i].valid = false;
        }
    }
}

static void InvalidateCache() {
    for (int i = 0; i < 16; i++) g_textureCache[i] = nullptr;
    for (int i = 0; i < 512; i++) g_renderStateValid[i] = false;
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 64; j++) g_textureStageStateValid[i][j] = false;
    }
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 32; j++) g_samplerStateValid[i][j] = false;
    }
    for (int i = 0; i < 512; i++) g_transformCache[i].valid = false;
    g_viewportValid = false;
    for (int i = 0; i < 256; i++) g_vsConstantCache[i].valid = false;
    CleanVBCache();
}



static HRESULT WINAPI Hooked_SetRenderState(IDirect3DDevice9* device, D3DRENDERSTATETYPE state, DWORD value) {
    if ((DWORD)state < 512) {
        if (g_renderStateValid[state] && g_renderStateCache[state] == value) {
            g_renderStateSkips.fetch_add(1, std::memory_order_relaxed);
            return D3D_OK;
        }
        g_renderStateCache[state] = value;
        g_renderStateValid[state] = true;
    }
    if (D3D9RenderThread::IsActive() && GetCurrentThreadId() == g_mainThreadId) {
        D3D9RenderThread::QueueSetRenderState(device, state, value);
        return D3D_OK;
    }
    return orig_SetRenderState(device, state, value);
}

static HRESULT WINAPI Hooked_SetTransform(IDirect3DDevice9* device, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix) {
    if ((DWORD)state < 512 && matrix) {
        if (g_transformCache[state].valid && memcmp(&g_transformCache[state].matrix, matrix, sizeof(D3DMATRIX)) == 0) {
            g_transformSkips.fetch_add(1, std::memory_order_relaxed);
            return D3D_OK;
        }
        memcpy(&g_transformCache[state].matrix, matrix, sizeof(D3DMATRIX));
        g_transformCache[state].valid = true;
    }
    if (D3D9RenderThread::IsActive() && GetCurrentThreadId() == g_mainThreadId) {
        D3D9RenderThread::QueueSetTransform(device, state, matrix);
        return D3D_OK;
    }
    return orig_SetTransform(device, state, matrix);
}

static HRESULT WINAPI Hooked_SetViewport(IDirect3DDevice9* device, const D3DVIEWPORT9* viewport) {
    if (viewport) {
        if (g_viewportValid && memcmp(&g_viewportCache, viewport, sizeof(D3DVIEWPORT9)) == 0) {
            g_viewportSkips.fetch_add(1, std::memory_order_relaxed);
            return D3D_OK;
        }
        memcpy(&g_viewportCache, viewport, sizeof(D3DVIEWPORT9));
        g_viewportValid = true;
    }
    if (D3D9RenderThread::IsActive() && GetCurrentThreadId() == g_mainThreadId) {
        D3D9RenderThread::QueueSetViewport(device, viewport);
        return D3D_OK;
    }
    return orig_SetViewport(device, viewport);
}

static HRESULT WINAPI Hooked_VB_Lock(IDirect3DVertexBuffer9* vb, UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) {
    D3D9RenderThread::PipelineFlush();
    #if !TEST_DISABLE_D3D9_VB_CACHE
    if (vb && ppbData && (Flags & D3DLOCK_DISCARD)) {
        unsigned int slot = HashVB(vb);
        ShadowBufferEntry* e = &g_vbCache[slot];
        
        if (e->valid && e->vb != vb) {
            if (e->data) _aligned_free(e->data);
            e->valid = false;
            e->data = nullptr;
        }
        
        if (!e->valid) {
            D3DVERTEXBUFFER_DESC desc;
            if (SUCCEEDED(vb->GetDesc(&desc))) {
                e->vb = vb;
                e->size = desc.Size;
                e->data = _aligned_malloc(desc.Size, 16);
                e->valid = true;
            }
        }
        
        if (e->valid && e->data) {
            *ppbData = (void*)((uintptr_t)e->data + OffsetToLock);
            return D3D_OK;
        }
    }
    #endif
    return orig_VB_Lock(vb, OffsetToLock, SizeToLock, ppbData, Flags);
}


static HRESULT WINAPI Hooked_VB_Unlock(IDirect3DVertexBuffer9* vb) {
    D3D9RenderThread::PipelineFlush();
    #if !TEST_DISABLE_D3D9_VB_CACHE
    if (vb) {
        unsigned int slot = HashVB(vb);
        ShadowBufferEntry* e = &g_vbCache[slot];
        if (e->valid && e->vb == vb && e->data) {
            void* realData = nullptr;
            HRESULT hr = orig_VB_Lock(vb, 0, e->size, &realData, D3DLOCK_DISCARD);
            if (SUCCEEDED(hr) && realData) {
                memcpy(realData, e->data, e->size);
                orig_VB_Unlock(vb);
            }
            return D3D_OK;
        }
    }
    #endif
    return orig_VB_Unlock(vb);
}

static HRESULT WINAPI Hooked_CreateVertexBuffer(IDirect3DDevice9* device, UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle) {
    HRESULT hr = orig_CreateVertexBuffer(device, Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle);
    if (hr == D3D_OK && ppVertexBuffer && *ppVertexBuffer && (Usage & D3DUSAGE_DYNAMIC)) {
        if (!g_vbHooksInstalled) {
            uintptr_t* vb_vtable = *(uintptr_t**)(*ppVertexBuffer);
            void* target_Lock = (void*)vb_vtable[11];
            void* target_Unlock = (void*)vb_vtable[12];
            
            if (MH_CreateHook(target_Lock, (void*)Hooked_VB_Lock, (void**)&orig_VB_Lock) == MH_OK) {
                MH_EnableHook(target_Lock);
            }
            if (MH_CreateHook(target_Unlock, (void*)Hooked_VB_Unlock, (void**)&orig_VB_Unlock) == MH_OK) {
                MH_EnableHook(target_Unlock);
            }
            g_vbHooksInstalled = true;
            Log("[D3D9StateCache] Detoured IDirect3DVertexBuffer9::Lock/Unlock for dynamic buffer optimization");
        }
    }
    return hr;
}

static HRESULT WINAPI Hooked_SetVertexShaderConstantF(IDirect3DDevice9* device, UINT StartRegister, const float* pConstantData, UINT Vector4fCount) {
    #if !TEST_DISABLE_D3D9_VS_CONSTANT_CACHE
    if (pConstantData && StartRegister + Vector4fCount <= 256) {
        bool allCached = true;
        for (UINT i = 0; i < Vector4fCount; i++) {
            UINT reg = StartRegister + i;
            if (!g_vsConstantCache[reg].valid || memcmp(g_vsConstantCache[reg].val, pConstantData + i * 4, 16) != 0) {
                allCached = false;
                break;
            }
        }
        
        if (allCached) {
            g_vsConstantSkips.fetch_add(Vector4fCount, std::memory_order_relaxed);
            return D3D_OK;
        }
        
        for (UINT i = 0; i < Vector4fCount; i++) {
            UINT reg = StartRegister + i;
            memcpy(g_vsConstantCache[reg].val, pConstantData + i * 4, 16);
            g_vsConstantCache[reg].valid = true;
        }
    }
    #endif
    if (D3D9RenderThread::IsActive() && GetCurrentThreadId() == g_mainThreadId) {
        D3D9RenderThread::QueueSetVertexShaderConstantF(device, StartRegister, pConstantData, Vector4fCount);
        return D3D_OK;
    }
    return orig_SetVertexShaderConstantF(device, StartRegister, pConstantData, Vector4fCount);
}

static HRESULT WINAPI Hooked_SetSamplerState(IDirect3DDevice9* device, DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) {
    if (Sampler < 16 && (DWORD)Type < 32) {
        if (g_samplerStateValid[Sampler][Type] && g_samplerStateCache[Sampler][Type] == Value) {
            g_samplerSkips.fetch_add(1, std::memory_order_relaxed);
            return D3D_OK;
        }
        g_samplerStateCache[Sampler][Type] = Value;
        g_samplerStateValid[Sampler][Type] = true;
    }

    #if !TEST_DISABLE_MIP_BIAS_GOVERNOR
    if (Type == 10 /* D3DSAMP_MIPMAPLODBIAS */) {
        float bias = MipBiasGovernor::GetCurrentBias();
        if (bias > 0.0f) {
            float floatVal = *(float*)&Value;
            floatVal += bias;
            Value = *(DWORD*)&floatVal;
        }
    }
    #endif

    if (D3D9RenderThread::IsActive() && GetCurrentThreadId() == g_mainThreadId) {
        D3D9RenderThread::QueueSetSamplerState(device, Sampler, Type, Value);
        return D3D_OK;
    }
    return orig_SetSamplerState(device, Sampler, Type, Value);
}

static HRESULT WINAPI Hooked_SetTextureStageState(IDirect3DDevice9* device, DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) {
    if (Stage < 8 && (DWORD)Type < 64) {
        if (g_textureStageStateValid[Stage][Type] && g_textureStageStateCache[Stage][Type] == Value) {
            g_stageStateSkips.fetch_add(1, std::memory_order_relaxed);
            return D3D_OK;
        }
        g_textureStageStateCache[Stage][Type] = Value;
        g_textureStageStateValid[Stage][Type] = true;
    }
    if (D3D9RenderThread::IsActive() && GetCurrentThreadId() == g_mainThreadId) {
        D3D9RenderThread::QueueSetTextureStageState(device, Stage, Type, Value);
        return D3D_OK;
    }
    return orig_SetTextureStageState(device, Stage, Type, Value);
}

static HRESULT WINAPI Hooked_Reset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params) {
    InvalidateCache();
    InvalidateLatencyQueries();
    Log("[D3D9StateCache] Device reset detected - cache and latency queries invalidated");
    if (D3D9RenderThread::IsActive() && GetCurrentThreadId() == g_mainThreadId) {
        D3D9RenderThread::QueueReset(device, params);
        return D3D_OK;
    }
    return orig_Reset(device, params);
}

static HRESULT WINAPI Hooked_Present(IDirect3DDevice9* device, const RECT* src, const RECT* dest, HWND window, const RGNDATA* dirty) {
    InvalidateCache();

    if (D3D9RenderThread::IsActive() && GetCurrentThreadId() == g_mainThreadId) {
        D3D9RenderThread::QueuePresent(device, src, dest, window, dirty);
        return D3D_OK;
    }

#if !TEST_DISABLE_LOW_LATENCY_SYNC
    if (device) {
        if (!g_latencyInitialized) {
            bool ok = true;
            for (int i = 0; i < LATENCY_QUEUE_SIZE; i++) {
                HRESULT hr = device->CreateQuery(D3DQUERYTYPE_EVENT, &g_latencyQueries[i]);
                if (FAILED(hr)) {
                    ok = false;
                    g_latencyQueries[i] = nullptr;
                }
            }
            if (ok) {
                g_latencyInitialized = true;
                Log("[D3D9StateCache] Low-latency GPU sync active (MaxFrameLatency = 1)");
            } else {
                InvalidateLatencyQueries();
            }
        }

        if (g_latencyInitialized) {
            IDirect3DQuery9* q = g_latencyQueries[g_latencyQueryIndex];
            if (q) {
                while (q->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {
                    SwitchToThread();
                }
            }

            IDirect3DQuery9* current_q = g_latencyQueries[g_latencyQueryIndex];
            if (current_q) {
                current_q->Issue(D3DISSUE_END);
            }

            g_latencyQueryIndex = (g_latencyQueryIndex + 1) % LATENCY_QUEUE_SIZE;
        }
    }
#endif

    return orig_Present(device, src, dest, window, dirty);
}

// Resolve virtual table offsets by creating a temporary dummy device
bool Init() {
#if TEST_DISABLE_D3D_STATE_CACHE
    Log("[D3D9StateCache] DISABLED via TEST_DISABLE_D3D_STATE_CACHE.");
    return false;
#endif

    InvalidateCache();

    HMODULE d3d9 = LoadLibraryA("d3d9.dll");
    if (!d3d9) {
        Log("[D3D9StateCache] Failed to load d3d9.dll");
        return false;
    }

    typedef IDirect3D9* (WINAPI *D3DCreate9_fn)(UINT);
    D3DCreate9_fn d3dCreate9 = (D3DCreate9_fn)GetProcAddress(d3d9, "Direct3DCreate9");
    if (!d3dCreate9) {
        Log("[D3D9StateCache] Failed to find Direct3DCreate9");
        return false;
    }

    IDirect3D9* d3d = d3dCreate9(D3D_SDK_VERSION);
    if (!d3d) {
        Log("[D3D9StateCache] Direct3DCreate9 failed");
        return false;
    }

    // Create a dummy window
    HWND hwnd = CreateWindowA("BUTTON", "dummy", WS_POPUP, 0, 0, 1, 1, NULL, NULL, NULL, NULL);
    if (!hwnd) {
        d3d->Release();
        return false;
    }

    D3DPRESENT_PARAMETERS params = {};
    params.Windowed = TRUE;
    params.SwapEffect = D3DSWAPEFFECT_DISCARD;
    params.hDeviceWindow = hwnd;

    IDirect3DDevice9* device = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                   D3DCREATE_SOFTWARE_VERTEXPROCESSING, &params, &device);
    if (FAILED(hr)) {
        DestroyWindow(hwnd);
        d3d->Release();
        Log("[D3D9StateCache] Failed to create dummy D3D9 device (hr=0x%08X)", hr);
        return false;
    }

    // Retrieve VTable addresses
    uintptr_t* vtable = *(uintptr_t**)device;
    void* target_Reset = (void*)vtable[16];
    void* target_Present = (void*)vtable[17];
    void* target_SetRenderState = (void*)vtable[57];
    void* target_SetTransform = (void*)vtable[44];
    void* target_SetViewport = (void*)vtable[47];
    void* target_CreateVertexBuffer = (void*)vtable[26];
    void* target_SetVertexShaderConstantF = (void*)vtable[94];
    void* target_SetSamplerState = (void*)vtable[69];
    void* target_SetTextureStageState = (void*)vtable[64];

    // Install hooks
    if (MH_CreateHook(target_Reset, (void*)Hooked_Reset, (void**)&orig_Reset) != MH_OK ||
        MH_CreateHook(target_Present, (void*)Hooked_Present, (void**)&orig_Present) != MH_OK ||
        MH_CreateHook(target_SetRenderState, (void*)Hooked_SetRenderState, (void**)&orig_SetRenderState) != MH_OK ||
        MH_CreateHook(target_SetTransform, (void*)Hooked_SetTransform, (void**)&orig_SetTransform) != MH_OK ||
        MH_CreateHook(target_SetViewport, (void*)Hooked_SetViewport, (void**)&orig_SetViewport) != MH_OK ||
        MH_CreateHook(target_CreateVertexBuffer, (void*)Hooked_CreateVertexBuffer, (void**)&orig_CreateVertexBuffer) != MH_OK ||
        MH_CreateHook(target_SetVertexShaderConstantF, (void*)Hooked_SetVertexShaderConstantF, (void**)&orig_SetVertexShaderConstantF) != MH_OK ||
        MH_CreateHook(target_SetSamplerState, (void*)Hooked_SetSamplerState, (void**)&orig_SetSamplerState) != MH_OK ||
        MH_CreateHook(target_SetTextureStageState, (void*)Hooked_SetTextureStageState, (void**)&orig_SetTextureStageState) != MH_OK) 
    {
        device->Release();
        DestroyWindow(hwnd);
        d3d->Release();
        Log("[D3D9StateCache] Failed to create MinHook detours");
        return false;
    }

    MH_EnableHook(target_Reset);
    MH_EnableHook(target_Present);
    MH_EnableHook(target_SetRenderState);
    MH_EnableHook(target_SetTransform);
    MH_EnableHook(target_SetViewport);
    MH_EnableHook(target_CreateVertexBuffer);
    MH_EnableHook(target_SetVertexShaderConstantF);
    MH_EnableHook(target_SetSamplerState);
    MH_EnableHook(target_SetTextureStageState);

    // Release dummy objects
    device->Release();
    DestroyWindow(hwnd);
    d3d->Release();

    Log("[D3D9StateCache] Active - Redundant render state filtering successfully hooked");
    return true;
}

void Shutdown() {
    InvalidateLatencyQueries();
    CleanVBCache();
    Log("[D3D9StateCache] Redundancy Skips: Textures: %ld, RenderStates: %ld, StageStates: %ld, Samplers: %ld, Transforms: %ld, Viewports: %ld, VSConstants: %ld",
        g_textureSkips.load(), g_renderStateSkips.load(), g_stageStateSkips.load(), g_samplerSkips.load(), g_transformSkips.load(), g_viewportSkips.load(), g_vsConstantSkips.load());
}

} // namespace D3D9StateCache
