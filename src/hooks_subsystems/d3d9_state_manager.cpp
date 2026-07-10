// ============================================================================
// Module: d3d9_state_manager.cpp
// Description: Deduplicates D3D9 device state changes and caches rendering states
//              to maximize CPU throughput and minimize driver overhead.
// Safety & Threading: Main render thread only. Crash-guarded against NULL pointers.
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
    V_SETTEXTURESTAGESTATE = 67,
    V_SETRENDERSTATE       = 57,
    V_SETTRANSFORM         = 44,
    V_SETMATERIAL          = 49,
    V_SETVIEWPORT          = 47,
    V_SETSCISSORRECT       = 75,
    V_SETSTREAMSOURCE      = 100,
    V_SETINDICES           = 104,
    V_SETVERTEXDECLARATION = 87,
    V_SETFVF               = 89,
    V_SETPIXELSHADER       = 107,
    V_SETVERTEXSHADER      = 92,
    V_SETTEXTURE           = 65,
};

static constexpr int NUM_HOOKS = 14;
static int g_vtableIndices[NUM_HOOKS] = {
    V_SETRENDERSTATE, V_SETTEXTURESTAGESTATE, V_SETSAMPLERSTATE,
    V_SETTEXTURE, V_SETTRANSFORM, V_SETMATERIAL,
    V_SETVIEWPORT, V_SETSCISSORRECT, V_SETSTREAMSOURCE,
    V_SETINDICES, V_SETVERTEXDECLARATION, V_SETFVF,
    V_SETVERTEXSHADER, V_SETPIXELSHADER
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
    "SetVertexShader", "SetPixelShader"
};

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
static bool     g_viewportValid = false;
static LONG     g_scissorData[4] = {};
static bool     g_scissorValid = false;
static void*  g_streamBuf[16] = {};
static UINT   g_streamOffset[16] = {};
static UINT   g_streamStride[16] = {};
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
// Fast matrix/material hash functions
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

// ================================================================
// Hooked functions
// ================================================================

static HRESULT __stdcall Hooked_SetRenderState(void* dev, DWORD state, DWORD value) {
    InterlockedIncrement64(&g_statCalls[0]);
    
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
    
    if (!matrix) {
        if (state < 32) g_xformValid[state] = false;
        return g_orig_SetTransform(dev, state, matrix);
    }

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

    if (!material) {
        g_materialValid = false;
        return g_orig_SetMaterial(dev, material);
    }

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

    if (!vp) {
        g_viewportValid = false;
        return g_orig_SetViewport(dev, vp);
    }

    if (g_viewportValid && memcmp(g_viewportData, vp, sizeof(g_viewportData)) == 0) {
        InterlockedIncrement64(&g_statSkipped[6]);
        return 0;
    }
    HRESULT hr = g_orig_SetViewport(dev, vp);
    if (SUCCEEDED(hr)) {
        memcpy(g_viewportData, vp, sizeof(g_viewportData));
        g_viewportValid = true;
    }
    return hr;
}

static HRESULT __stdcall Hooked_SetScissorRect(void* dev, const RECT* rect) {
    InterlockedIncrement64(&g_statCalls[7]);

    if (!rect) {
        g_scissorValid = false;
        return g_orig_SetScissorRect(dev, rect);
    }

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

    if (stream < 16 && g_streamValid[stream] && g_streamBuf[stream] == vb && g_streamOffset[stream] == offset && g_streamStride[stream] == stride) {
        InterlockedIncrement64(&g_statSkipped[8]);
        return 0;
    }
    HRESULT hr = g_orig_SetStreamSource(dev, stream, vb, offset, stride);
    if (SUCCEEDED(hr) && stream < 16) {
        g_streamBuf[stream] = vb;
        g_streamOffset[stream] = offset;
        g_streamStride[stream] = stride;
        g_streamValid[stream] = true;
    }
    return hr;
}

static HRESULT __stdcall Hooked_SetIndices(void* dev, void* ib) {
    InterlockedIncrement64(&g_statCalls[9]);

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
    (void*)Hooked_SetPixelShader
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

    g_pDevice = pDevice;
    g_deviceHooked = true;
    Log("[D3D9State] Device vtable patched: %d/%d state hooks installed", patched, NUM_HOOKS);
    return true;
}

static void UnpatchDeviceVTable() {
    if (!g_pDevice || !g_deviceHooked) return;

    uintptr_t* vtable = *(uintptr_t**)g_pDevice;
    if (!vtable) { g_deviceHooked = false; g_pDevice = nullptr; return; }

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
    g_viewportValid = false;
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
    memset(g_streamOffset, 0, sizeof(g_streamOffset));
    memset(g_streamStride, 0, sizeof(g_streamStride));
    memset(g_streamValid, 0, sizeof(g_streamValid));
    memset((void*)g_statCalls, 0, sizeof(g_statCalls));
    memset((void*)g_statSkipped, 0, sizeof(g_statSkipped));
    g_totalFrames = 0;

    bool ok = TryFindAndPatchDevice();
    if (ok) {
        Log("[D3D9State] [ OK ] Device vtable patched (%d hooks)", NUM_HOOKS);
    } else {
        Log("[D3D9State] Device not found at init — retrying each frame");
    }

    return true;
}

void ShutdownD3D9StateManager(void) {
    UnpatchDeviceVTable();

    Log("[D3D9State] ===== Final Statistics (%lld frames) =====", g_totalFrames);
    if (g_totalFrames > 0) {
        for (int i = 0; i < NUM_HOOKS; i++) {
            Log("[D3D9State]   %-22s: calls=%lld skipped=%lld (%.1f%%)",
                g_statNames[i], g_statCalls[i], g_statSkipped[i],
                g_statCalls[i] > 0 ? (double)g_statSkipped[i] * 100.0 / g_statCalls[i] : 0.0);
        }
    }
}

void OnFrameD3D9StateManager(DWORD mainThreadId) {
    if (GetCurrentThreadId() != mainThreadId) return;

    g_totalFrames++;
    
    // Invalidate state cache every frame to ensure synchronization with device resets,
    // window resizing, resolution changes, and DXVK state updates!
    InvalidateAllCaches();
    
    // Check if the device pointer has changed
    uintptr_t addr = ADDR_CGXDEVICED3D_PTR;
    if (addr != 0 && IsReadable(addr)) {
        uintptr_t pGxDevice = *(uintptr_t*)addr;
        if (pGxDevice && IsReadable(pGxDevice)) {
            uintptr_t devicePtrAddr = pGxDevice + 0x397C;
            if (IsReadable(devicePtrAddr)) {
                void* pDevice = *(void**)devicePtrAddr;
                if (pDevice != g_pDevice) {
                    Log("[D3D9State] Device pointer changed from %p to %p. Re-patching.", g_pDevice, pDevice);
                    UnpatchDeviceVTable();
                    if (pDevice && IsReadable((uintptr_t)pDevice)) {
                        PatchDeviceVTable(pDevice);
                    }
                }
            }
        }
    }
    
    if (!g_deviceHooked) {
        TryFindAndPatchDevice();
    }
}
