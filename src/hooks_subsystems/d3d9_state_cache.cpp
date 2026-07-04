// ============================================================================
// Module: d3d9_state_cache.cpp
// Description: Filters redundant D3D9 state modifications to reduce context switches.
// Safety & Threading: Main thread only. Invalidates cache on Reset().
// ============================================================================

#include "d3d9_state_cache.h"
#include "MinHook.h"
#include <d3d9.h>
#include <atomic>

extern "C" void Log(const char* fmt, ...);

namespace D3D9StateCache {

// Original function pointers
typedef HRESULT (WINAPI *SetTexture_fn)(IDirect3DDevice9* device, DWORD stage, IDirect3DBaseTexture9* texture);
static SetTexture_fn orig_SetTexture = nullptr;

typedef HRESULT (WINAPI *SetRenderState_fn)(IDirect3DDevice9* device, D3DRENDERSTATETYPE state, DWORD value);
static SetRenderState_fn orig_SetRenderState = nullptr;

typedef HRESULT (WINAPI *SetTextureStageState_fn)(IDirect3DDevice9* device, DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value);
static SetTextureStageState_fn orig_SetTextureStageState = nullptr;

typedef HRESULT (WINAPI *SetSamplerState_fn)(IDirect3DDevice9* device, DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value);
static SetSamplerState_fn orig_SetSamplerState = nullptr;

typedef HRESULT (WINAPI *Reset_fn)(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params);
static Reset_fn orig_Reset = nullptr;

typedef HRESULT (WINAPI *Present_fn)(IDirect3DDevice9* device, const RECT* src, const RECT* dest, HWND window, const RGNDATA* dirty);
static Present_fn orig_Present = nullptr;

// Cache structures
static IDirect3DBaseTexture9* g_textureCache[16] = { nullptr };

static DWORD g_renderStateCache[512] = { 0 };
static bool g_renderStateValid[512] = { false };

static DWORD g_textureStageStateCache[8][64] = { {0} };
static bool g_textureStageStateValid[8][64] = { {false} };

static DWORD g_samplerStateCache[16][32] = { {0} };
static bool g_samplerStateValid[16][32] = { {false} };

// Statistics
static std::atomic<long> g_textureSkips{0};
static std::atomic<long> g_renderStateSkips{0};
static std::atomic<long> g_stageStateSkips{0};
static std::atomic<long> g_samplerSkips{0};

// Clear the cache (called on Init and after device Reset)
static void InvalidateCache() {
    for (int i = 0; i < 16; i++) g_textureCache[i] = nullptr;
    for (int i = 0; i < 512; i++) g_renderStateValid[i] = false;
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 64; j++) g_textureStageStateValid[i][j] = false;
    }
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 32; j++) g_samplerStateValid[i][j] = false;
    }
}

// Hooked SetTexture
static HRESULT WINAPI Hooked_SetTexture(IDirect3DDevice9* device, DWORD stage, IDirect3DBaseTexture9* texture) {
    if (stage < 16) {
        if (g_textureCache[stage] == texture) {
            g_textureSkips.fetch_add(1, std::memory_order_relaxed);
            return D3D_OK;
        }
        g_textureCache[stage] = texture;
    }
    return orig_SetTexture(device, stage, texture);
}

// Hooked SetRenderState
static HRESULT WINAPI Hooked_SetRenderState(IDirect3DDevice9* device, D3DRENDERSTATETYPE state, DWORD value) {
    if ((DWORD)state < 512) {
        if (g_renderStateValid[state] && g_renderStateCache[state] == value) {
            g_renderStateSkips.fetch_add(1, std::memory_order_relaxed);
            return D3D_OK;
        }
        g_renderStateCache[state] = value;
        g_renderStateValid[state] = true;
    }
    return orig_SetRenderState(device, state, value);
}

// Hooked SetTextureStageState
static HRESULT WINAPI Hooked_SetTextureStageState(IDirect3DDevice9* device, DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) {
    if (stage < 8 && (DWORD)type < 64) {
        if (g_textureStageStateValid[stage][type] && g_textureStageStateCache[stage][type] == value) {
            g_stageStateSkips.fetch_add(1, std::memory_order_relaxed);
            return D3D_OK;
        }
        g_textureStageStateCache[stage][type] = value;
        g_textureStageStateValid[stage][type] = true;
    }
    return orig_SetTextureStageState(device, stage, type, value);
}

// Hooked SetSamplerState
static HRESULT WINAPI Hooked_SetSamplerState(IDirect3DDevice9* device, DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value) {
    if (sampler < 16 && (DWORD)type < 32) {
        if (g_samplerStateValid[sampler][type] && g_samplerStateCache[sampler][type] == value) {
            g_samplerSkips.fetch_add(1, std::memory_order_relaxed);
            return D3D_OK;
        }
        g_samplerStateCache[sampler][type] = value;
        g_samplerStateValid[sampler][type] = true;
    }
    return orig_SetSamplerState(device, sampler, type, value);
}

// Hooked Reset
static HRESULT WINAPI Hooked_Reset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params) {
    InvalidateCache();
    Log("[D3D9StateCache] Device reset detected - cache invalidated");
    return orig_Reset(device, params);
}

// Hooked Present
static HRESULT WINAPI Hooked_Present(IDirect3DDevice9* device, const RECT* src, const RECT* dest, HWND window, const RGNDATA* dirty) {
    InvalidateCache();
    return orig_Present(device, src, dest, window, dirty);
}

// Resolve virtual table offsets by creating a temporary dummy device
bool Init() {
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
    void* target_SetTexture = (void*)vtable[65];
    void* target_SetTextureStageState = (void*)vtable[67];
    void* target_SetSamplerState = (void*)vtable[69];

    // Install hooks
    if (MH_CreateHook(target_Reset, (void*)Hooked_Reset, (void**)&orig_Reset) != MH_OK ||
        MH_CreateHook(target_Present, (void*)Hooked_Present, (void**)&orig_Present) != MH_OK ||
        MH_CreateHook(target_SetRenderState, (void*)Hooked_SetRenderState, (void**)&orig_SetRenderState) != MH_OK ||
        MH_CreateHook(target_SetTexture, (void*)Hooked_SetTexture, (void**)&orig_SetTexture) != MH_OK ||
        MH_CreateHook(target_SetTextureStageState, (void*)Hooked_SetTextureStageState, (void**)&orig_SetTextureStageState) != MH_OK ||
        MH_CreateHook(target_SetSamplerState, (void*)Hooked_SetSamplerState, (void**)&orig_SetSamplerState) != MH_OK) 
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
    MH_EnableHook(target_SetTexture);
    MH_EnableHook(target_SetTextureStageState);
    MH_EnableHook(target_SetSamplerState);

    // Release dummy objects
    device->Release();
    DestroyWindow(hwnd);
    d3d->Release();

    Log("[D3D9StateCache] Active - Redundant render state filtering successfully hooked");
    return true;
}

void Shutdown() {
    MH_DisableHook(MH_ALL_HOOKS); // Safely disable all hooks
    Log("[D3D9StateCache] Redundancy Skips: Textures: %ld, RenderStates: %ld, StageStates: %ld, Samplers: %ld",
        g_textureSkips.load(), g_renderStateSkips.load(), g_stageStateSkips.load(), g_samplerSkips.load());
}

} // namespace D3D9StateCache
