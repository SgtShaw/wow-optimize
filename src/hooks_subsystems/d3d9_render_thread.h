#pragma once
#include <windows.h>
#include <d3d9.h>

namespace D3D9RenderThread {
    bool Init();
    void Shutdown();
    void OnFrame();
    void PipelineFlush();
    bool IsActive();
    DWORD GetRenderThreadId();

    // Queue APIs called from hooked wrappers
    void QueueSetRenderState(IDirect3DDevice9* device, D3DRENDERSTATETYPE state, DWORD value);
    void QueueSetTransform(IDirect3DDevice9* device, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix);
    void QueueSetViewport(IDirect3DDevice9* device, const D3DVIEWPORT9* viewport);
    void QueueSetVertexShaderConstantF(IDirect3DDevice9* device, UINT startRegister, const float* constantData, UINT vector4fCount);
    void QueueSetSamplerState(IDirect3DDevice9* device, DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value);
    void QueueSetTextureStageState(IDirect3DDevice9* device, DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value);
    void QueueReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params);
    void QueuePresent(IDirect3DDevice9* device, const RECT* src, const RECT* dest, HWND window, const RGNDATA* dirty);
}
