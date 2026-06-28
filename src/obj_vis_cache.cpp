// ================================================================
// Object Visibility Cache - Implementation
//
// Hooks sub_4D4BB0 (WoW's object hash table lookup, 111 bytes)
// and sub_4D4C20 (hash node unlink/removal function) to prevent
// Use-After-Free crashes of despawned units.
// ================================================================

#include "obj_vis_cache.h"
#include "version.h"
#include "MinHook.h"
#include <cstring>
#include <intrin.h>

extern "C" void Log(const char* fmt, ...);

#if !TEST_DISABLE_OBJ_VIS_CACHE

static constexpr int CACHE_SIZE = 128;
static constexpr int CACHE_MASK = CACHE_SIZE - 1;
static constexpr int MAX_THREADS = 32;

struct CacheEntry {
    void*    thisPtr;      // Hash table owner ptr (safety check)
    uint32_t hashKey;
    uint32_t guidLow;
    uint32_t guidHigh;
    void*    result;
    uint32_t frameStamp;
};

struct ThreadCacheSlot {
    volatile LONG  tid;          // 0 = unused, otherwise thread ID
    CacheEntry     entries[CACHE_SIZE];
    uint32_t       lastFrame;
};

static ThreadCacheSlot g_cachePool[MAX_THREADS];
static volatile LONG g_frameCounter = 0;
static volatile LONG g_poolInitDone = 0;

static volatile LONG g_cacheHits = 0;
static volatile LONG g_cacheMisses = 0;
static volatile LONG g_poolExhausted = 0;

typedef void* (__thiscall *HashLookup_fn)(void* thisPtr, uint32_t hashKey, void* guidPtr);
static HashLookup_fn orig_HashLookup = nullptr;

typedef void* (__thiscall *UnlinkNode_fn)(void* This);
static UnlinkNode_fn orig_UnlinkNode = nullptr;

static inline uint32_t HashKey(uint32_t hashKey, uint32_t low, uint32_t high) {
    uint32_t h = hashKey ^ (low * 0x9E3779B9u) ^ (high * 0x85EBCA6Bu);
    h ^= h >> 16;
    h *= 0xC2B2AE35u;
    h ^= h >> 13;
    return h;
}

static ThreadCacheSlot* GetThreadCacheSlot() {
    DWORD tid = GetCurrentThreadId();

    for (int i = 0; i < MAX_THREADS; i++) {
        if ((DWORD)g_cachePool[i].tid == tid) {
            return &g_cachePool[i];
        }
    }

    for (int i = 0; i < MAX_THREADS; i++) {
        if (InterlockedCompareExchange(&g_cachePool[i].tid, (LONG)tid, 0) == 0) {
            memset((void*)g_cachePool[i].entries, 0, sizeof(g_cachePool[i].entries));
            g_cachePool[i].lastFrame = 0;
            return &g_cachePool[i];
        }
    }

    InterlockedIncrement(&g_poolExhausted);
    return nullptr;
}

static void* __fastcall hooked_HashLookup(
    void* thisPtr, void* /*edx*/, uint32_t hashKey, void* guidPtr)
{
    if (!thisPtr || !guidPtr) {
        return orig_HashLookup(thisPtr, hashKey, guidPtr);
    }

    uint32_t guidLow, guidHigh;
    __try {
        guidLow  = *(uint32_t*)guidPtr;
        guidHigh = *((uint32_t*)guidPtr + 1);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        return orig_HashLookup(thisPtr, hashKey, guidPtr);
    }

    ThreadCacheSlot* slot = GetThreadCacheSlot();
    if (!slot) {
        return orig_HashLookup(thisPtr, hashKey, guidPtr);
    }

    uint32_t currentFrame = (uint32_t)InterlockedCompareExchange(&g_frameCounter, 0, 0);
    uint32_t idx = HashKey(hashKey, guidLow, guidHigh) & CACHE_MASK;
    CacheEntry* entry = &slot->entries[idx];

    if (entry->frameStamp == currentFrame &&
        entry->thisPtr == thisPtr &&
        entry->hashKey == hashKey &&
        entry->guidLow == guidLow &&
        entry->guidHigh == guidHigh)
    {
        InterlockedIncrement(&g_cacheHits);
        return entry->result;
    }

    void* result = orig_HashLookup(thisPtr, hashKey, guidPtr);

    entry->thisPtr = thisPtr;
    entry->hashKey = hashKey;
    entry->guidLow = guidLow;
    entry->guidHigh = guidHigh;
    entry->result = result;
    entry->frameStamp = currentFrame;

    InterlockedIncrement(&g_cacheMisses);
    return result;
}

extern "C" void InvalidateDeferredFieldUpdatesFor(void* unit);

static void* __fastcall hooked_UnlinkNode(void* This, void* /*edx*/) {
    if (This && g_poolInitDone) {
        uint32_t* j = (uint32_t*)This;
        __try {
            uint32_t hashKey = j[6];
            uint32_t guidLow = j[12];
            uint32_t guidHigh = j[13];
            
            // Invalidate the cache entry across all threads
            uint32_t idx = HashKey(hashKey, guidLow, guidHigh) & CACHE_MASK;
            for (int i = 0; i < MAX_THREADS; i++) {
                CacheEntry* entry = &g_cachePool[i].entries[idx];
                if (entry->hashKey == hashKey &&
                    entry->guidLow == guidLow &&
                    entry->guidHigh == guidHigh)
                {
                    entry->frameStamp = 0; // invalidate slot
                }
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
        
        // Invalidate any deferred field updates for this unit
        InvalidateDeferredFieldUpdatesFor(This);
    }
    return orig_UnlinkNode(This);
}

namespace ObjVisCache {

bool Init() {
    Log("[ObjVisCache] Init");

    memset((void*)g_cachePool, 0, sizeof(g_cachePool));
    InterlockedExchange(&g_poolInitDone, 1);

    void* target = (void*)0x4D4BB0;
    void* target_unlink = (void*)0x4D4C20;

    if (MH_CreateHook(target, (void*)hooked_HashLookup, (void**)&orig_HashLookup) != MH_OK) {
        Log("[ObjVisCache] ERROR: MH_CreateHook failed at 0x4D4BB0");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[ObjVisCache] ERROR: MH_EnableHook failed");
        MH_RemoveHook(target);
        return false;
    }

    if (MH_CreateHook(target_unlink, (void*)hooked_UnlinkNode, (void**)&orig_UnlinkNode) != MH_OK) {
        Log("[ObjVisCache] ERROR: MH_CreateHook failed at 0x4D4C20");
        MH_DisableHook(target);
        MH_RemoveHook(target);
        return false;
    }
    if (MH_EnableHook(target_unlink) != MH_OK) {
        Log("[ObjVisCache] ERROR: MH_EnableHook failed at 0x4D4C20");
        MH_DisableHook(target);
        MH_RemoveHook(target);
        MH_RemoveHook(target_unlink);
        return false;
    }

    Log("[ObjVisCache] [ OK ] Hooked lookup/unlink, static pool=%d threads (no alloc)", MAX_THREADS);
    return true;
}

void Shutdown() {
    LONG hits = g_cacheHits;
    LONG misses = g_cacheMisses;
    LONG total = hits + misses;
    double rate = total > 0 ? (100.0 * hits / total) : 0.0;

    Log("[ObjVisCache] Shutdown (hits=%d, misses=%d, rate=%.1f%%, exhausted=%d)",
        hits, misses, rate, g_poolExhausted);

    void* target = (void*)0x4D4BB0;
    void* target_unlink = (void*)0x4D4C20;

    MH_DisableHook(target);
    MH_RemoveHook(target);
    MH_DisableHook(target_unlink);
    MH_RemoveHook(target_unlink);

    InterlockedExchange(&g_poolInitDone, 0);
}

void OnFrame() {
    InterlockedIncrement(&g_frameCounter);
}

} // namespace ObjVisCache

#else  // TEST_DISABLE_OBJ_VIS_CACHE

namespace ObjVisCache {
bool Init() { Log("[ObjVisCache] DISABLED (feature flag)"); return false; }
void Shutdown() {}
void OnFrame() {}
} // namespace ObjVisCache

#endif
