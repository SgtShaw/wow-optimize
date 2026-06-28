// ============================================================================
// Module: tls_object_cache.cpp
// Description: Supporting utility functions for `tls_object_cache.cpp`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================

#include "tls_object_cache.h"
#include "MinHook.h"
#include "version.h"
#include <cstdint>
#include <intrin.h>

extern "C" void Log(const char* fmt, ...);

// sub_4D3790: TLS accessor returning game object pointer at offset 192
// Called 1297 times per frame cycle - hot leaf function
// Original: reads TLS index, dereferences TEB, returns *(ptr+192)

static volatile LONG g_tlsObjHits = 0;
static volatile LONG g_tlsObjMisses = 0;

typedef __int64 (__cdecl *GetTlsObject_fn)();
static GetTlsObject_fn orig_GetTlsObject = nullptr;

static __declspec(thread) __int64 t_cachedTlsObj = 0;
static __declspec(thread) uint32_t t_tlsObjValid = 0;

static __int64 __cdecl Hooked_GetTlsObject() {
    if (t_tlsObjValid) {
        _InterlockedIncrement(&g_tlsObjHits);
        return t_cachedTlsObj;
    }
    __int64 result = orig_GetTlsObject();
    t_cachedTlsObj = result;
    t_tlsObjValid = 1;
    _InterlockedIncrement(&g_tlsObjMisses);
    return result;
}

void TlsObjectCache_Invalidate() {
    t_tlsObjValid = 0;
}

namespace TlsObjectCache {
    bool Install() {
        void* target = (void*)0x004D3790;
        if (WineSafe_CreateHook(target, (void*)Hooked_GetTlsObject, (void**)&orig_GetTlsObject) != MH_OK) {
            Log("[TlsObjCache] Hook failed");
            return false;
        }
        if (MH_EnableHook(target) != MH_OK) return false;
        Log("[TlsObjCache] ACTIVE (1297 xrefs, TLS+192 cached per-thread)");
        return true;
    }

    void Shutdown() {
        Log("[TlsObjCache] hits=%d misses=%d", g_tlsObjHits, g_tlsObjMisses);
    }
}