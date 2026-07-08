// ============================================================================
// Module: lua_alloc_pool.cpp
// Description: Thread-local allocation cache for Lua VM small temporary objects.
// Safety & Threading: Thread-safe, implements local free lists per thread.
// ============================================================================

#include "lua_alloc_pool.h"
#include "MinHook.h"
#include "version.h"
#include <windows.h>
#include <cstring>

extern "C" void Log(const char* fmt, ...);

namespace LuaAllocPool {

typedef void* (__cdecl *LuaAlloc_fn)(void* ud, void* ptr, size_t osize, size_t nsize);
static LuaAlloc_fn orig_LuaAlloc = nullptr;

struct ThreadCache {
    void* allocator_ud;
    void* free_lists[6]; // Linked lists (first 4 bytes store next pointer)
    int   counts[6];
};

static __declspec(thread) ThreadCache* t_cache = nullptr;
static constexpr int MAX_BLOCKS_PER_LIST = 64;

static inline int GetBucket(size_t size) {
    if (size <= 8) return 0;
    if (size <= 16) return 1;
    if (size <= 32) return 2;
    if (size <= 64) return 3;
    if (size <= 128) return 4;
    if (size <= 256) return 5;
    return -1;
}

static inline size_t GetBucketSize(int bucket) {
    switch (bucket) {
        case 0: return 8;
        case 1: return 16;
        case 2: return 32;
        case 3: return 64;
        case 4: return 128;
        case 5: return 256;
        default: return 0;
    }
}

static ThreadCache* GetThreadCache() {
    if (!t_cache) {
        t_cache = (ThreadCache*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ThreadCache));
    }
    return t_cache;
}

static void* __cdecl Hooked_LuaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
#if TEST_DISABLE_LUA_POOL_LF
    return orig_LuaAlloc(ud, ptr, osize, nsize);
#else
    ThreadCache* tc = GetThreadCache();
    if (!tc) {
        return orig_LuaAlloc(ud, ptr, osize, nsize);
    }

    if (tc->allocator_ud != ud) {
        // Discard stale cached blocks from the previous Lua state/allocator
        for (int b = 0; b < 6; b++) {
            tc->free_lists[b] = nullptr;
            tc->counts[b] = 0;
        }
        tc->allocator_ud = ud;
    }

    // CASE 1: Free
    if (nsize == 0) {
        if (ptr) {
            int bucket = GetBucket(osize);
            if (bucket >= 0 && osize == GetBucketSize(bucket) && tc->counts[bucket] < MAX_BLOCKS_PER_LIST) {
                // Add to thread-local free list
                *(void**)ptr = tc->free_lists[bucket];
                tc->free_lists[bucket] = ptr;
                tc->counts[bucket]++;
                return nullptr;
            }
            return orig_LuaAlloc(ud, ptr, osize, 0);
        }
        return nullptr;
    }

    // CASE 2: Allocation
    if (ptr == nullptr) {
        int bucket = GetBucket(nsize);
        if (bucket >= 0) {
            if (tc->free_lists[bucket]) {
                void* block = tc->free_lists[bucket];
                tc->free_lists[bucket] = *(void**)block;
                tc->counts[bucket]--;
                memset(block, 0, GetBucketSize(bucket));
                return block;
            }
            // Allocate bucket-sized block using original allocator to avoid fragmentation
            size_t bsize = GetBucketSize(bucket);
            return orig_LuaAlloc(ud, nullptr, 0, bsize);
        }
        return orig_LuaAlloc(ud, nullptr, 0, nsize);
    }

    // CASE 3: Reallocation
    int oldBucket = GetBucket(osize);
    int newBucket = GetBucket(nsize);

    if (oldBucket >= 0 && newBucket >= 0 && oldBucket == newBucket && osize == GetBucketSize(oldBucket)) {
        // Size remains in the same bucket - no action needed
        return ptr;
    }

    // Fallback: allocate new, copy, and free
    size_t newSize = newBucket >= 0 ? GetBucketSize(newBucket) : nsize;
    void* newPtr = Hooked_LuaAlloc(ud, nullptr, 0, newSize);
    if (newPtr) {
        size_t copySize = osize < nsize ? osize : nsize;
        memcpy(newPtr, ptr, copySize);
        Hooked_LuaAlloc(ud, ptr, osize, 0);
    }
    return newPtr;
#endif
}

bool Init() {
    void* target = (void*)0x008558E0;
    
    unsigned char prologue[3];
    __try {
        memcpy(prologue, target, 3);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("[LuaAllocPool] Target 0x008558E0 not readable.");
        return true;
    }

    // __cdecl prologue: 55 8B EC
    if (prologue[0] != 0x55 || prologue[1] != 0x8B || prologue[2] != 0xEC) {
        Log("[LuaAllocPool] Bad prologue at 0x008558E0. Skipping hook.");
        return true;
    }

    if (MH_CreateHook(target, (void*)Hooked_LuaAlloc, (void**)&orig_LuaAlloc) == MH_OK) {
        if (MH_EnableHook(target) == MH_OK) {
            Log("[LuaAllocPool] Detour active at 0x008558E0.");
            return true;
        }
        MH_RemoveHook(target);
    }

    Log("[LuaAllocPool] Active - Lua VM Thread-Local Allocator Pool initialized.");
    return true;
}

void Shutdown() {
    void* target = (void*)0x008558E0;
    MH_DisableHook(target);
    
    // Clean up current thread cache
    if (t_cache) {
        for (int b = 0; b < 6; b++) {
            void* block = t_cache->free_lists[b];
            while (block) {
                void* next = *(void**)block;
                // Free back to standard heap
                orig_LuaAlloc(nullptr, block, GetBucketSize(b), 0);
                block = next;
            }
        }
        HeapFree(GetProcessHeap(), 0, t_cache);
        t_cache = nullptr;
    }
}

} // namespace LuaAllocPool
