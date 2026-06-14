/*
 * TLS Pointer Cache - Eliminate 1297+ TEB lookups per frame
 * 
 * WoW's sub_4D3790 reads Thread Local Storage on every call.
 * With 1297 callers × 60 FPS = 77,820 calls/sec, each doing TEB lookup.
 * 
 * Optimization: Cache TEB pointer and TLS slot per thread (one-time read).
 * Expected savings: ~200 cycles × 77,820 calls = 15.5M cycles/sec
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <atomic>
#include "MinHook.h"

extern "C" void Log(const char* fmt, ...);

// WoW's TLS index stored at this address in .data segment
#define WOW_TLS_INDEX_ADDR  0x00D439BC

// Thread-local cached values (never change after first read)
static __declspec(thread) void* g_cachedTeb = nullptr;
static __declspec(thread) void* g_cachedTlsSlot = nullptr;
static __declspec(thread) bool g_tlsCached = false;

// Statistics (diagnostic only; plain increments -- this runs ~77k times/sec,
// a locked RMW per call would cost far more than the cached TLS read itself)
static volatile uint64_t g_tlsCacheHits = 0;
static volatile uint64_t g_tlsCacheTotal = 0;

// Original function
typedef __int64 (__cdecl *TlsAccessor_fn)();
static TlsAccessor_fn g_originalTlsAccessor = nullptr;
static bool g_hookInstalled = false;

// sub_4D4DB0 hook (TLS + type check)
typedef int (__cdecl *TlsTypeCheck_fn)(__int64 a1, int a2);
static TlsTypeCheck_fn g_original4D4DB0 = nullptr;
static bool g_hook4D4DB0Installed = false;

// Hooked version - caches TEB and TLS slot
__int64 __cdecl Hooked_TlsAccessor()
{
    ++g_tlsCacheTotal;

    // Fast path: use cached TEB and TLS slot
    if (g_tlsCached && g_cachedTeb && g_cachedTlsSlot) {
        ++g_tlsCacheHits;
        
        // Read current value (might change)
        void* tlsData = *(void**)((uint8_t*)g_cachedTlsSlot + 8);
        if (tlsData) {
            return *(__int64*)((uint8_t*)tlsData + 192);
        }
        return 0;
    }
    
    // Slow path: first call on this thread - cache TEB and TLS slot
    g_cachedTeb = NtCurrentTeb();
    if (!g_cachedTeb) {
        return 0;
    }
    
    // TEB->ThreadLocalStoragePointer at offset 0x2C on x86
    void** tlsArray = *(void***)((uint8_t*)g_cachedTeb + 0x2C);
    
    // Read WoW's TLS index
    DWORD tlsIdx = *(DWORD*)WOW_TLS_INDEX_ADDR;
    g_cachedTlsSlot = tlsArray[tlsIdx];
    
    if (!g_cachedTlsSlot) {
        return 0;
    }
    
    g_tlsCached = true;
    
    // Return the value at offset +192
    void* tlsData = *(void**)((uint8_t*)g_cachedTlsSlot + 8);
    if (tlsData) {
        return *(__int64*)((uint8_t*)tlsData + 192);
    }
    
    return 0;
}

// sub_4D4DB0 hook - TLS + type check
int __cdecl Hooked_4D4DB0(__int64 a1, int a2)
{
    // Fast path: use cached TLS
    if (g_tlsCached && g_cachedTeb && g_cachedTlsSlot) {
        void* tlsData = *(void**)((uint8_t*)g_cachedTlsSlot + 8);
        if (!tlsData) return 0;
        if (!a1) return 0;
        
        // Call sub_4D4BB0 equivalent (inline the type check logic)
        __int64 v3 = a1;
        int result = (int)g_original4D4DB0(a1, a2); // Still call original for now

        if (result && (a2 & *(DWORD *)(*(DWORD *)(result + 8) + 8)) == 0) {
            return 0;
        }
        return result;
    }
    
    // Slow path: cache TLS first
    g_cachedTeb = NtCurrentTeb();
    if (!g_cachedTeb) return g_original4D4DB0 ? g_original4D4DB0(a1, a2) : 0;
    
    void** tlsArray = *(void***)((uint8_t*)g_cachedTeb + 0x2C);
    DWORD tlsIdx = *(DWORD*)WOW_TLS_INDEX_ADDR;
    g_cachedTlsSlot = tlsArray[tlsIdx];
    
    if (!g_cachedTlsSlot) return g_original4D4DB0 ? g_original4D4DB0(a1, a2) : 0;
    
    g_tlsCached = true;
    return g_original4D4DB0 ? g_original4D4DB0(a1, a2) : 0;
}

bool InstallTlsCache()
{
    if (g_hookInstalled) {
        return true;
    }

    void* targetAddr = (void*)0x004D3790;

    // Don't check prologue - this function is tiny (43 bytes) with no stack frame
    // Just install the hook directly

    if (MH_CreateHook(targetAddr,
                      (void*)Hooked_TlsAccessor,
                      (void**)&g_originalTlsAccessor) != MH_OK)
    {
        Log("[TLSCache] Failed to create hook at 0x004D3790");
        return false;
    }

    if (MH_EnableHook(targetAddr) != MH_OK) {
        Log("[TLSCache] Failed to enable hook");
        MH_RemoveHook(targetAddr);
        return false;
    }

    g_hookInstalled = true;
    Log("[TLSCache] Hook installed: sub_4D3790 @ 0x004D3790 (200+ callers, TEB caching)");
    
    // Install sub_4D4DB0 hook
    if (!g_hook4D4DB0Installed) {
        void* target4D4DB0 = (void*)0x004D4DB0;
        if (MH_CreateHook(target4D4DB0, (void*)Hooked_4D4DB0, (void**)&g_original4D4DB0) == MH_OK &&
            MH_EnableHook(target4D4DB0) == MH_OK) {
            g_hook4D4DB0Installed = true;
            Log("[TLSCache] Hook installed: sub_4D4DB0 @ 0x004D4DB0 (200+ callers, TLS+type check)");
        } else {
            Log("[TLSCache] Failed to hook sub_4D4DB0");
        }
    }

    return true;
}

void UninstallTlsCache()
{
    if (!g_hookInstalled) {
        return;
    }
    
    MH_DisableHook((void*)0x004D3790);
    MH_RemoveHook((void*)0x004D3790);
    
    uint64_t hits = g_tlsCacheHits;
    uint64_t total = g_tlsCacheTotal;

    if (total > 0) {
        double hitRate = (double)hits / total * 100.0;
        Log("[TLSCache] Stats: %llu/%llu cache hits (%.1f%%)",
            hits, total, hitRate);
    }
    
    g_hookInstalled = false;
}

void GetTlsCacheStats(uint64_t* hits, uint64_t* total)
{
    if (hits) *hits = g_tlsCacheHits;
    if (total) *total = g_tlsCacheTotal;
}
