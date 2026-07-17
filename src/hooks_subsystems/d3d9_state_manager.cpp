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
#include "font_glyph_cache.h"
#include "texture_unload_delay.h"
#include "d3d9_state_cache.h"
#include "render_state_dedup.h"
#include "win_mutex.h"

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
    V_RESET                = 16,
};

static constexpr int NUM_HOOKS = 15;
static int g_vtableIndices[NUM_HOOKS] = {
    V_SETRENDERSTATE, V_SETTEXTURESTAGESTATE, V_SETSAMPLERSTATE,
    V_SETTEXTURE, V_SETTRANSFORM, V_SETMATERIAL,
    V_SETVIEWPORT, V_SETSCISSORRECT, V_SETSTREAMSOURCE,
    V_SETINDICES, V_SETVERTEXDECLARATION, V_SETFVF,
    V_SETVERTEXSHADER, V_SETPIXELSHADER, V_RESET
};

static void* g_vtableOriginals[NUM_HOOKS] = {};
static bool  g_vtablePatched[NUM_HOOKS] = {};

static void* g_pDevice = nullptr;
static void* g_pPatchedVTable = nullptr;
static bool  g_deviceHooked = false;
volatile LONG g_deviceResetCounter = 0;

// Guards vtable read-modify-write in PatchDeviceVTable/UnpatchDeviceVTable. Without
// this, the init thread's first-time patch and the game thread's CheckDeviceChange
// re-patch can race on the same VirtualProtect'd page: one thread restores the
// page to non-writable between another thread's protect and its write, faulting
// on the vtable-slot store (observed as ACCESS_VIOLATION inside PatchDeviceVTable
// at startup, offset 0x3E9C0, both threads logging "Device vtable patched" within
// the same millisecond).
static WinMutex g_vtableMutex;

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
    "SetVertexShader", "SetPixelShader", "Reset"
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

static void InvalidateAllCaches();
static void UnpatchDeviceVTable();
static bool PatchDeviceVTable(void* pDevice);

static inline void CheckDeviceChange(void* dev) {
    // Do NOT bypass this under DXVK: it's the only mechanism that notices a
    // device reset/recreation (windowed<->fullscreen, resize) and invalidates
    // FontGlyphCache/TextureUnloadDelay/D3D9StateCache. Skipping it here left
    // those caches hanging onto descriptors for destroyed textures after any
    // display-mode change, corrupting all on-screen text. The vtable-patch
    // race that DXVKBridge::IsActive() was introduced to dodge is fixed at
    // the source now (g_vtableMutex in PatchDeviceVTable/UnpatchDeviceVTable).
    if (dev && dev != g_pDevice) {
        Log("[D3D9State] Real-time Device pointer change detected (old: %p, new: %p).", g_pDevice, dev);
        
        InterlockedIncrement(&g_deviceResetCounter);
        InvalidateAllCaches();
        RenderStateDedup_ClearCache();
        #ifndef TEST_DISABLE_FONT_METRICS_FAST
        FontGlyphCache::ClearCache();
        #endif
        TextureUnloadDelay::Discard();
        D3D9StateCache::InvalidateAllCaches(false);

        g_pDevice = dev;

        g_deviceHooked = false; // Force PatchDeviceVTable to run and verify/re-hook the new device vtable
        PatchDeviceVTable(dev);
    }
}

// ================================================================
// Fast matrix/material hash functions
// ================================================================
static uint64_t QuickMatrixHash(const float* m) {
    uint64_t h = 0;
    const uint32_t* p = (const uint32_t*)m;
    for (int i = 0; i < 16; i++) {
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

typedef HRESULT (__stdcall *Reset_t)(void* dev, D3DPRESENT_PARAMETERS* params);
static Reset_t g_orig_Reset = nullptr;

// ================================================================
// Hooked functions
// ================================================================

static HRESULT __stdcall Hooked_SetRenderState(void* dev, DWORD state, DWORD value) {
    CheckDeviceChange(dev);
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
    CheckDeviceChange(dev);
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
    CheckDeviceChange(dev);
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
    CheckDeviceChange(dev);
    InterlockedIncrement64(&g_statCalls[3]);
    // Caching resource pointers is unsafe due to address recycling. Always call original.
    return g_orig_SetTexture(dev, stage, tex);
}

static HRESULT __stdcall Hooked_SetTransform(void* dev, DWORD state, const void* matrix) {
    CheckDeviceChange(dev);
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
    CheckDeviceChange(dev);
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
    CheckDeviceChange(dev);
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
    CheckDeviceChange(dev);
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
    CheckDeviceChange(dev);
    InterlockedIncrement64(&g_statCalls[8]);
    // Caching resource pointers is unsafe due to address recycling. Always call original.
    return g_orig_SetStreamSource(dev, stream, vb, offset, stride);
}

static HRESULT __stdcall Hooked_SetIndices(void* dev, void* ib) {
    CheckDeviceChange(dev);
    InterlockedIncrement64(&g_statCalls[9]);
    // Caching resource pointers is unsafe due to address recycling. Always call original.
    return g_orig_SetIndices(dev, ib);
}

static HRESULT __stdcall Hooked_SetVertexDeclaration(void* dev, void* decl) {
    CheckDeviceChange(dev);
    InterlockedIncrement64(&g_statCalls[10]);
    // Caching resource pointers is unsafe due to address recycling. Always call original.
    return g_orig_SetVertexDeclaration(dev, decl);
}

static HRESULT __stdcall Hooked_SetFVF(void* dev, DWORD fvf) {
    CheckDeviceChange(dev);
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
    CheckDeviceChange(dev);
    InterlockedIncrement64(&g_statCalls[12]);
    // Caching resource pointers is unsafe due to address recycling. Always call original.
    return g_orig_SetVertexShader(dev, vs);
}

static HRESULT __stdcall Hooked_SetPixelShader(void* dev, void* ps) {
    CheckDeviceChange(dev);
    InterlockedIncrement64(&g_statCalls[13]);
    // Caching resource pointers is unsafe due to address recycling. Always call original.
    return g_orig_SetPixelShader(dev, ps);
}

static HRESULT __stdcall Hooked_Reset(void* dev, D3DPRESENT_PARAMETERS* params) {
    CheckDeviceChange(dev);
    InterlockedIncrement64(&g_statCalls[14]);
    Log("[D3D9State] Device Reset detected! Invalidating all caches and flushing delayed textures...");
    InvalidateAllCaches();
    RenderStateDedup_ClearCache();
    D3D9StateCache::InvalidateAllCaches(true);
    
    // Clear font glyph cache
    #ifndef TEST_DISABLE_FONT_METRICS_FAST
    FontGlyphCache::ClearCache();
    #endif

    // Flush delayed textures
    TextureUnloadDelay::Discard();

    InterlockedIncrement(&g_deviceResetCounter);

    HRESULT hr = g_orig_Reset(dev, params);
    if (SUCCEEDED(hr)) {
        InvalidateAllCaches();
        RenderStateDedup_ClearCache();
        D3D9StateCache::InvalidateAllCaches(true);
        #ifndef TEST_DISABLE_FONT_METRICS_FAST
        FontGlyphCache::ClearCache();
        #endif
        InterlockedIncrement(&g_deviceResetCounter);
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
    (void*)Hooked_SetPixelShader,
    (void*)Hooked_Reset
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
        case 14: g_orig_Reset                 = (Reset_t)orig; break;
        default: break;
    }
}

#ifndef ADDR_CGXDEVICED3D_PTR
#define ADDR_CGXDEVICED3D_PTR  0x00C5DF88  // global CGxDeviceD3d*
#endif

static bool PatchDeviceVTable(void* pDevice) {
    WinLockGuard lock(g_vtableMutex);
    if (!pDevice || g_deviceHooked) return false;

    uintptr_t* vtable = *(uintptr_t**)pDevice;
    if (!vtable || !IsReadable((uintptr_t)vtable)) return false;

    int patched = 0;
    for (int i = 0; i < NUM_HOOKS; i++) {
        int vtIndex = g_vtableIndices[i];
        if (!g_hookFuncs[i]) continue;

        uintptr_t origFunc = vtable[vtIndex];
        if (!IsReadable(origFunc)) {
            Log("[D3D9State] Skipping vtable[%d] — original not readable", vtIndex);
            continue;
        }

        // Avoid infinite recursion: check if the vtable entry is already pointing to our hook
        if (origFunc == (uintptr_t)g_hookFuncs[i]) {
            g_vtablePatched[i] = true;
            patched++;
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
    g_pPatchedVTable = vtable;
    g_deviceHooked = true;
    InterlockedIncrement(&g_deviceResetCounter);
    Log("[D3D9State] Device vtable patched: %d/%d state hooks installed (vtable: %p, resetCounter: %ld)", patched, NUM_HOOKS, vtable, g_deviceResetCounter);
    return true;
}

// Split from UnpatchDeviceVTable() because MSVC forbids __try in a function
// that also has a C++ object needing unwinding (WinLockGuard) — C2712.
static void UnpatchDeviceVTableInner() {
    __try {
        uintptr_t* vtable = (uintptr_t*)g_pPatchedVTable;
        if (!IsReadable((uintptr_t)vtable)) {
            return;
        }

        // Restore state hooks in reverse order
        for (int i = NUM_HOOKS - 1; i >= 0; i--) {
            if (!g_vtablePatched[i]) continue;
            int vtIndex = g_vtableIndices[i];

            // Safety: verify that the target vtable address is still readable
            if (!IsReadable((uintptr_t)&vtable[vtIndex])) continue;

            // Verify that the vtable currently points to our hook before restoring it,
            // to prevent overwriting third-party hooks or crashing
            if (vtable[vtIndex] == (uintptr_t)g_hookFuncs[i]) {
                DWORD oldProtect;
                if (VirtualProtect(&vtable[vtIndex], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                    vtable[vtIndex] = (uintptr_t)g_vtableOriginals[i];
                    VirtualProtect(&vtable[vtIndex], sizeof(void*), oldProtect, &oldProtect);
                }
            }
            g_vtablePatched[i] = false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("[D3D9State] SEH exception caught during UnpatchDeviceVTable!");
    }
}

static void UnpatchDeviceVTable() {
    WinLockGuard lock(g_vtableMutex);
    if (!g_deviceHooked || !g_pPatchedVTable) return;

    UnpatchDeviceVTableInner();

    g_deviceHooked = false;
    g_pDevice = nullptr;
    g_pPatchedVTable = nullptr;
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
    // Always patch, even under DXVK: this is what installs the Reset hook that
    // FontGlyphCache/TextureUnloadDelay/D3D9StateCache rely on for invalidation
    // (see CheckDeviceChange). DXVKBridge::IsActive() used to bypass this
    // entirely, leaving no way to detect device resets under DXVK at all.
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
    
    if (!g_deviceHooked) {
        TryFindAndPatchDevice();
    }
}
