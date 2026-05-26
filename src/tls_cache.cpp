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

// Statistics
static std::atomic<uint64_t> g_tlsCacheHits{0};
static std::atomic<uint64_t> g_tlsCacheTotal{0};

// Original function
typedef __int64 (__cdecl *TlsAccessor_fn)();
static TlsAccessor_fn g_originalTlsAccessor = nullptr;
static bool g_hookInstalled = false;

// Hooked version - caches TEB and TLS slot
__int64 __cdecl Hooked_TlsAccessor()
{
    g_tlsCacheTotal.fetch_add(1, std::memory_order_relaxed);
    
    // Fast path: use cached TEB and TLS slot
    if (g_tlsCached && g_cachedTeb && g_cachedTlsSlot) {
        g_tlsCacheHits.fetch_add(1, std::memory_order_relaxed);
        
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
    Log("[TLSCache] Hook installed: sub_4D3790 @ 0x004D3790 (1297 callers, TEB caching)");
    
    return true;
}

void UninstallTlsCache()
{
    if (!g_hookInstalled) {
        return;
    }
    
    MH_DisableHook((void*)0x004D3790);
    MH_RemoveHook((void*)0x004D3790);
    
    uint64_t hits = g_tlsCacheHits.load();
    uint64_t total = g_tlsCacheTotal.load();
    
    if (total > 0) {
        double hitRate = (double)hits / total * 100.0;
        Log("[TLSCache] Stats: %llu/%llu cache hits (%.1f%%)",
            hits, total, hitRate);
    }
    
    g_hookInstalled = false;
}

void GetTlsCacheStats(uint64_t* hits, uint64_t* total)
{
    if (hits) *hits = g_tlsCacheHits.load(std::memory_order_relaxed);
    if (total) *total = g_tlsCacheTotal.load(std::memory_order_relaxed);
}
