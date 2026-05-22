// ================================================================
// Object Visibility Cache - Implementation
//
// Hooks sub_4D4BB0 (WoW's object hash table lookup, 111 bytes,
// zero internal calls, pure function).
//
// SAFETY: Uses static pre-allocated cache pool indexed by TID.
// NO VirtualAlloc/TlsAlloc in hot path to avoid loader lock deadlock.
// ================================================================

#include "obj_vis_cache.h"
#include "version.h"
#include "MinHook.h"
#include <cstring>
#include <intrin.h>

extern "C" void Log(const char* fmt, ...);

#if !TEST_DISABLE_OBJ_VIS_CACHE

// ================================================================
// Static cache pool - no dynamic allocation in hot path
// ================================================================
static constexpr int CACHE_SIZE = 128;
static constexpr int CACHE_MASK = CACHE_SIZE - 1;
static constexpr int MAX_THREADS = 32;  // WoW rarely exceeds 16 threads

struct CacheEntry {
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

// Pre-allocated static pool - zero runtime allocation
static ThreadCacheSlot g_cachePool[MAX_THREADS];
static volatile LONG g_frameCounter = 0;
static volatile LONG g_poolInitDone = 0;

// Statistics
static volatile LONG g_cacheHits = 0;
static volatile LONG g_cacheMisses = 0;
static volatile LONG g_poolExhausted = 0;

// ================================================================
// Original function type
// ================================================================
typedef void* (__thiscall *HashLookup_fn)(void* thisPtr, uint32_t hashKey, void* guidPtr);
static HashLookup_fn orig_HashLookup = nullptr;

// ================================================================
// Cache helpers - NO dynamic allocation
// ================================================================
static inline uint32_t HashKey(uint32_t hashKey, uint32_t low, uint32_t high) {
    uint32_t h = hashKey ^ (low * 0x9E3779B9u) ^ (high * 0x85EBCA6Bu);
    h ^= h >> 16;
    h *= 0xC2B2AE35u;
    h ^= h >> 13;
    return h;
}

// Find or claim a cache slot for current thread.
// Uses InterlockedCompareExchange for lock-free thread-safe slot claiming.
// Returns NULL if pool exhausted (graceful degradation).
static ThreadCacheSlot* GetThreadCacheSlot() {
    DWORD tid = GetCurrentThreadId();

    // Fast path: find existing slot
    for (int i = 0; i < MAX_THREADS; i++) {
        if ((DWORD)g_cachePool[i].tid == tid) {
            return &g_cachePool[i];
        }
    }

    // Slow path: claim an empty slot (only happens once per thread)
    for (int i = 0; i < MAX_THREADS; i++) {
        if (InterlockedCompareExchange(&g_cachePool[i].tid, (LONG)tid, 0) == 0) {
            // Successfully claimed - zero the entries
            memset((void*)g_cachePool[i].entries, 0, sizeof(g_cachePool[i].entries));
            g_cachePool[i].lastFrame = 0;
            return &g_cachePool[i];
        }
    }

    // Pool exhausted - graceful degradation (no caching for this thread)
    InterlockedIncrement(&g_poolExhausted);
    return nullptr;
}

// ================================================================
// Hooked function
// ================================================================
static void* __fastcall hooked_HashLookup(
    void* thisPtr, void* /*edx*/, uint32_t hashKey, void* guidPtr)
{
    // Fast reject
    if (!thisPtr || !guidPtr) {
        return orig_HashLookup(thisPtr, hashKey, guidPtr);
    }

    // Read GUID pair from caller's stack-local struct
    uint32_t guidLow, guidHigh;
    __try {
        guidLow  = *(uint32_t*)guidPtr;
        guidHigh = *((uint32_t*)guidPtr + 1);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        return orig_HashLookup(thisPtr, hashKey, guidPtr);
    }

    // Get cache slot (lock-free, no allocation)
    ThreadCacheSlot* slot = GetThreadCacheSlot();
    if (!slot) {
        return orig_HashLookup(thisPtr, hashKey, guidPtr);
    }

    uint32_t currentFrame = (uint32_t)InterlockedCompareExchange(&g_frameCounter, 0, 0);
    uint32_t idx = HashKey(hashKey, guidLow, guidHigh) & CACHE_MASK;
    CacheEntry* entry = &slot->entries[idx];

    if (entry->frameStamp == currentFrame &&
        entry->hashKey == hashKey &&
        entry->guidLow == guidLow &&
        entry->guidHigh == guidHigh)
    {
        InterlockedIncrement(&g_cacheHits);
        return entry->result;
    }

    // Cache miss
    void* result = orig_HashLookup(thisPtr, hashKey, guidPtr);

    entry->hashKey = hashKey;
    entry->guidLow = guidLow;
    entry->guidHigh = guidHigh;
    entry->result = result;
    entry->frameStamp = currentFrame;

    InterlockedIncrement(&g_cacheMisses);
    return result;
}

// ================================================================
// Public API
// ================================================================
namespace ObjVisCache {

bool Init() {
    Log("[ObjVisCache] Init");

    // Zero the static pool (no allocation!)
    memset((void*)g_cachePool, 0, sizeof(g_cachePool));
    InterlockedExchange(&g_poolInitDone, 1);

    void* target = (void*)0x4D4BB0;
    if (MH_CreateHook(target, (void*)hooked_HashLookup, (void**)&orig_HashLookup) != MH_OK) {
        Log("[ObjVisCache] ERROR: MH_CreateHook failed at 0x4D4BB0");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[ObjVisCache] ERROR: MH_EnableHook failed");
        MH_RemoveHook(target);
        return false;
    }

    Log("[ObjVisCache] [ OK ] Hooked 0x4D4BB0, static pool=%d slots x %d entries (no alloc)",
        MAX_THREADS, CACHE_SIZE);
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
    MH_DisableHook(target);
    MH_RemoveHook(target);

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
