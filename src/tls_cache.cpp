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
#include "lua_optimize.h"   // LuaOpt::IsReloading / IsSwapping

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

// sub_4D4DB0 = ClntObjMgrObjectPtr (GUID + type mask -> object pointer)
typedef int (__cdecl *TlsTypeCheck_fn)(__int64 a1, int a2);
static TlsTypeCheck_fn g_original4D4DB0 = nullptr;
static bool g_hook4D4DB0Installed = false;

// Object GUID -> pointer cache.
// ClntObjMgrObjectPtr resolves a 64-bit GUID (+ type mask) to an object pointer
// via a hash walk (sub_4D4BB0). Addons hammer it thousands of times per frame
// (UnitHealth/UnitName/UnitGUID/...). We cache positive results and, on every
// hit, content-validate by re-reading the object's own stored GUID -- the exact
// dwords sub_4D4BB0 matches on (result[12]==guidLo, result[13]==guidHi, i.e.
// byte offsets +48/+52). A freed or recycled object therefore never matches, so
// a stale pointer can never be returned. The read is SEH-guarded in case the
// cached object's memory was unmapped, and we bypass the cache entirely during
// VM reload/teardown when the object manager is unstable.
static const int OBJ_CACHE_SIZE = 8192;
static const int OBJ_CACHE_MASK = OBJ_CACHE_SIZE - 1;
struct ObjGuidEntry { uint32_t lo; uint32_t hi; int mask; int result; };
static ObjGuidEntry g_objCache[OBJ_CACHE_SIZE] = {};
static volatile uint64_t g_objCacheHits = 0;
static volatile uint64_t g_objCacheMisses = 0;

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

// sub_4D4DB0 hook - object GUID -> pointer cache (content-validated)
int __cdecl Hooked_4D4DB0(__int64 a1, int a2)
{
    if (!a1 || !g_original4D4DB0) return g_original4D4DB0 ? g_original4D4DB0(a1, a2) : 0;

    // Object manager is unstable while the VM is being swapped/reloaded.
    if (LuaOpt::IsReloading() || LuaOpt::IsSwapping())
        return g_original4D4DB0(a1, a2);

    uint32_t lo = (uint32_t)a1;
    uint32_t hi = (uint32_t)((uint64_t)a1 >> 32);
    ObjGuidEntry* e = &g_objCache[(lo ^ hi ^ (uint32_t)a2) & OBJ_CACHE_MASK];

    if (e->result && e->lo == lo && e->hi == hi && e->mask == a2) {
        uintptr_t r = (uintptr_t)(unsigned)e->result;
        if (r > 0x10000 && r < 0xBFFF0000) {
            __try {
                // Same compare sub_4D4BB0 uses to identify the row: the object's
                // own GUID dwords. If it still matches, the object is alive.
                if (*(uint32_t*)(r + 48) == lo && *(uint32_t*)(r + 52) == hi) {
                    ++g_objCacheHits;
                    return e->result;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        // Stale/freed/recycled -> fall through and re-resolve.
    }

    ++g_objCacheMisses;
    int result = (int)g_original4D4DB0(a1, a2);
    if (result) { e->lo = lo; e->hi = hi; e->mask = a2; e->result = result; }
    return result;
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

    // ObjGuidCache (sub_4D4DB0 / ClntObjMgrObjectPtr) is permanently disabled.
    // World-exit teardown (sub_528C30 -> sub_5D9D90) resolves each object via this
    // function and immediately does a virtual call through the returned pointer.
    // During teardown objects are unlinked from the manager (the real function then
    // returns 0) BEFORE their stored GUID dwords are scrubbed, so a cached pointer to
    // a half-destructed object still passes GUID content-validation -> we hand back a
    // dangling pointer -> virtual call on a freed vtable -> deterministic ACCESS_VIOLATION
    // at 0x5D9DD1 on every logout. Caching this function is unsound: it must always
    // reflect live manager membership, which a content check on the object alone cannot.
    (void)&Hooked_4D4DB0;
    Log("[ObjGuidCache] DISABLED: caching ClntObjMgrObjectPtr returns stale object "
        "pointers during world-exit teardown (crash at 0x5D9DD1 on logout)");

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

    uint64_t objHits = g_objCacheHits, objMiss = g_objCacheMisses, objTot = objHits + objMiss;
    if (objTot > 0) {
        Log("[ObjGuidCache] Stats: %llu/%llu hits (%.1f%%)",
            objHits, objTot, 100.0 * objHits / objTot);
    }

    g_hookInstalled = false;
}

void GetTlsCacheStats(uint64_t* hits, uint64_t* total)
{
    if (hits) *hits = g_tlsCacheHits;
    if (total) *total = g_tlsCacheTotal;
}
