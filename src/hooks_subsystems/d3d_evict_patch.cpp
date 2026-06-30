// ============================================================================
// Module: d3d_evict_patch.cpp
// Description: Installs and manages target intercepts for subsystem `d3d_evict_patch.cpp`.
// Safety & Threading: Stack layouts and register conventions must match target function definitions exactly.
// ============================================================================

#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "d3d_evict_patch.h"

extern "C" void Log(const char* fmt, ...);

typedef void (__fastcall *EvictCheck_t)(void* ecx, void* edx);
static EvictCheck_t g_origEvictD3d = nullptr;
static EvictCheck_t g_origEvictD3dEx = nullptr;

static void __fastcall Hooked_EvictD3d(void* ecx, void* edx)
{
    char* deviceBase = (char*)ecx;
    if (!deviceBase) return;

    __try {
        uint32_t active = *(uint32_t*)(deviceBase + 3928);
        void* d3d9Device = *(void**)(deviceBase + 14716);

        if (active && d3d9Device) {
            uintptr_t* vtable = *(uintptr_t**)d3d9Device;
            typedef HRESULT (__stdcall *EvictManagedResources_t)(void*);
            EvictManagedResources_t evictFn = (EvictManagedResources_t)vtable[5];
            
            HRESULT hr = evictFn(d3d9Device);
            if (hr == (HRESULT)0x88760807) { // D3DERR_DEVICEREMOVED
                static bool loggedOnce = false;
                if (!loggedOnce) {
                    Log("[D3DEvict] Suppressed D3D9 driver internal error on EvictManagedResources (device removed)");
                    loggedOnce = true;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void __fastcall Hooked_EvictD3dEx(void* ecx, void* edx)
{
    char* deviceBase = (char*)ecx;
    if (!deviceBase) return;

    __try {
        uint32_t active = *(uint32_t*)(deviceBase + 3928);
        void* d3d9Device = *(void**)(deviceBase + 14716);

        if (active && d3d9Device) {
            uintptr_t* vtable = *(uintptr_t**)d3d9Device;
            typedef HRESULT (__stdcall *EvictManagedResources_t)(void*);
            EvictManagedResources_t evictFn = (EvictManagedResources_t)vtable[5];
            
            HRESULT hr = evictFn(d3d9Device);
            if (hr == (HRESULT)0x88760807) { // D3DERR_DEVICEREMOVED
                static bool loggedOnce = false;
                if (!loggedOnce) {
                    Log("[D3DEvict] Suppressed D3D9Ex driver internal error on EvictManagedResources (device removed)");
                    loggedOnce = true;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

bool InstallD3DEvictPatch()
{
    bool ok = true;

    // CGxD3dDevice::EvictManagedResources check (0x68E450)
    void* t1 = (void*)0x0068E450;
    if (MH_CreateHook(t1, (void*)Hooked_EvictD3d, (void**)&g_origEvictD3d) == MH_OK) {
        if (MH_EnableHook(t1) == MH_OK) {
            Log("[D3DEvict] D3D9 EvictManagedResources check suppressed");
        } else { ok = false; Log("[D3DEvict] D3D9 enable FAILED"); }
    } else { ok = false; Log("[D3DEvict] D3D9 CreateHook FAILED"); }

    // CGxD3d9ExDevice::EvictManagedResources check (0x69FDC0)
    void* t2 = (void*)0x0069FDC0;
    if (MH_CreateHook(t2, (void*)Hooked_EvictD3dEx, (void**)&g_origEvictD3dEx) == MH_OK) {
        if (MH_EnableHook(t2) == MH_OK) {
            Log("[D3DEvict] D3D9Ex EvictManagedResources check suppressed");
        } else { ok = false; Log("[D3DEvict] D3D9Ex enable FAILED"); }
    } else { ok = false; Log("[D3DEvict] D3D9Ex CreateHook FAILED"); }

    return ok;
}
