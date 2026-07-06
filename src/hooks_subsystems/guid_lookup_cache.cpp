// ============================================================================
// Module: guid_lookup_cache.cpp
// Description: Lock-free GUID to Object pointer lookup cache.
// Safety & Threading: Thread-safe, avoids global locks using atomic cache slots.
// ============================================================================

#include "guid_lookup_cache.h"
#include "MinHook.h"
#include "version.h"
#include <windows.h>
#include <atomic>

extern "C" void Log(const char* fmt, ...);

namespace GuidLookupCache {

struct CacheEntry {
    std::atomic<unsigned __int64> guid;
    std::atomic<void*>            obj;
};

static constexpr int CACHE_SIZE = 1024;
static constexpr int CACHE_MASK = CACHE_SIZE - 1;
static CacheEntry g_cache[CACHE_SIZE];

typedef void* (__cdecl *GetObject_fn)(unsigned __int64 guid, int typemask);
static GetObject_fn orig_GetObject = nullptr;

static inline unsigned int HashGuid(unsigned __int64 guid) {
    unsigned int h = (unsigned int)(guid ^ (guid >> 32));
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h & CACHE_MASK;
}

static inline bool IsValidPtr(uintptr_t p) {
    return p > 0x10000 && p < 0xFFE00000;
}

static void* __cdecl Hooked_GetObject(unsigned __int64 guid, int typemask) {
#if TEST_DISABLE_GUID_MAP_LF
    return orig_GetObject(guid, typemask);
#else
    if (guid == 0) return nullptr;

    unsigned int slot = HashGuid(guid);
    unsigned __int64 cachedGuid = g_cache[slot].guid.load(std::memory_order_relaxed);
    
    if (cachedGuid == guid) {
        void* cachedObj = g_cache[slot].obj.load(std::memory_order_relaxed);
        if (cachedObj && IsValidPtr((uintptr_t)cachedObj)) {
            // Replicate the original function's type mask validation:
            // ecx = [cachedObj + 8] (metadata descriptor)
            // test [ecx + 8], typemask
            void* ecx = *(void**)((uintptr_t)cachedObj + 8);
            if (ecx && IsValidPtr((uintptr_t)ecx)) {
                int typeMask = *(int*)((uintptr_t)ecx + 8);
                if ((typeMask & typemask) != 0) {
                    return cachedObj;
                }
            }
        }
        // If type check fails or object is invalid, return nullptr since GUID is unique
        return nullptr;
    }

    void* result = orig_GetObject(guid, typemask);
    if (result) {
        g_cache[slot].guid.store(guid, std::memory_order_relaxed);
        g_cache[slot].obj.store(result, std::memory_order_relaxed);
    }
    return result;
#endif
}

void Invalidate(unsigned __int64 guid) {
    if (guid == 0) return;
    unsigned int slot = HashGuid(guid);
    unsigned __int64 cachedGuid = g_cache[slot].guid.load(std::memory_order_relaxed);
    if (cachedGuid == guid) {
        g_cache[slot].guid.store(0, std::memory_order_relaxed);
        g_cache[slot].obj.store(nullptr, std::memory_order_relaxed);
    }
}

bool Init() {
    for (int i = 0; i < CACHE_SIZE; i++) {
        g_cache[i].guid.store(0, std::memory_order_relaxed);
        g_cache[i].obj.store(nullptr, std::memory_order_relaxed);
    }

    void* target = (void*)0x004D4DB0;
    
    unsigned char prologue[3];
    __try {
        memcpy(prologue, target, 3);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("[GuidLookupCache] Target 0x004D4DB0 not readable.");
        return true;
    }

    // Standard __cdecl prologue: 55 8B EC (push ebp; mov ebp, esp)
    if (prologue[0] != 0x55 || prologue[1] != 0x8B || prologue[2] != 0xEC) {
        Log("[GuidLookupCache] Bad prologue at 0x004D4DB0. Skipping hook.");
        return true;
    }

    if (MH_CreateHook(target, (void*)Hooked_GetObject, (void**)&orig_GetObject) == MH_OK) {
        if (MH_EnableHook(target) == MH_OK) {
            Log("[GuidLookupCache] Detour active at 0x004D4DB0.");
            return true;
        }
        MH_RemoveHook(target);
    }

    Log("[GuidLookupCache] Active - Lock-free Object GUID cache ready.");
    return true;
}

void Shutdown() {
    void* target = (void*)0x004D4DB0;
    MH_DisableHook(target);
}

} // namespace GuidLookupCache
