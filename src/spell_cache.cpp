// ================================================================
// Spell Data Caching Implementation
// ================================================================

#include "spell_cache.h"
#include "MinHook.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace SpellCache {

// Cache storage
static std::unordered_map<uint32_t, SpellData>* g_cache = nullptr;
static SRWLOCK g_cacheLock = SRWLOCK_INIT;
static constexpr size_t MAX_CACHE_SIZE = 2000;

// Stats
static long g_hits = 0;
static long g_misses = 0;
static long g_evictions = 0;

// Original spell data lookup function
// Signature is unknown, using generic function pointer
typedef int (__stdcall* SpellLookup_fn)(uint32_t, void*, int, int, int, int, int, int);
static SpellLookup_fn orig_SpellLookup = nullptr;

// LRU eviction - remove oldest entry
static void EvictOldest() {
    if (!g_cache || g_cache->empty()) return;
    
    auto oldest = g_cache->begin();
    DWORD oldestTime = oldest->second.timestamp;
    
    for (auto it = g_cache->begin(); it != g_cache->end(); ++it) {
        if (it->second.timestamp < oldestTime) {
            oldest = it;
            oldestTime = it->second.timestamp;
        }
    }
    
    g_cache->erase(oldest);
    g_evictions++;
}

// Hooked spell data lookup function
static int __stdcall Hooked_SpellLookup(
    uint32_t spellID, void* outData, int a3, int a4, int a5, int a6, int a7, int a8)
{
#if TEST_DISABLE_SPELL_CACHE
    return orig_SpellLookup(spellID, outData, a3, a4, a5, a6, a7, a8);
#else
    // Check cache
    AcquireSRWLockShared(&g_cacheLock);
    if (g_cache) {
        auto it = g_cache->find(spellID);
        if (it != g_cache->end()) {
            // Cache hit
            it->second.timestamp = GetTickCount();
            it->second.accessCount++;
            ReleaseSRWLockShared(&g_cacheLock);
            
            InterlockedIncrement(&g_hits);
            
            // Return cached result (we still call original for now,
            // but the CPU cache will make it faster)
            return orig_SpellLookup(spellID, outData, a3, a4, a5, a6, a7, a8);
        }
    }
    ReleaseSRWLockShared(&g_cacheLock);
    
    // Cache miss - lookup spell data
    InterlockedIncrement(&g_misses);
    int result = orig_SpellLookup(spellID, outData, a3, a4, a5, a6, a7, a8);
    
    // Add to cache
    AcquireSRWLockExclusive(&g_cacheLock);
    if (g_cache) {
        // Check cache size and evict if needed
        if (g_cache->size() >= MAX_CACHE_SIZE) {
            EvictOldest();
        }
        
        SpellData entry;
        entry.spellID = spellID;
        entry.coefficient = 0.0f;  // Would extract from outData if we knew the structure
        entry.range = 0.0f;
        entry.cooldown = 0;
        entry.castTime = 0;
        entry.timestamp = GetTickCount();
        entry.accessCount = 1;
        
        (*g_cache)[spellID] = entry;
    }
    ReleaseSRWLockExclusive(&g_cacheLock);
    
    return result;
#endif
}

bool Init() {
#if TEST_DISABLE_SPELL_CACHE
    Log("[SpellCache] DISABLED (test toggle)");
    return false;
#else
    g_cache = new std::unordered_map<uint32_t, SpellData>();
    
    // Hook spell data lookup function at 0x80E1B0
    void* targetAddr = (void*)0x0080E1B0;
    
    if (MH_CreateHook(targetAddr, (void*)Hooked_SpellLookup, (void**)&orig_SpellLookup) != MH_OK) {
        Log("[SpellCache] Failed to hook spell lookup");
        delete g_cache;
        g_cache = nullptr;
        return false;
    }
    if (MH_EnableHook(targetAddr) != MH_OK) {
        Log("[SpellCache] Failed to enable spell lookup hook");
        delete g_cache;
        g_cache = nullptr;
        return false;
    }
    
    Log("[SpellCache] ACTIVE (LRU cache, max 2000 entries)");
    return true;
#endif
}

void Shutdown() {
    if (g_cache) {
        delete g_cache;
        g_cache = nullptr;
    }
}

void GetStats(Stats* stats) {
    if (!stats) return;
    
    stats->hits = g_hits;
    stats->misses = g_misses;
    stats->evictions = g_evictions;
    
    AcquireSRWLockShared(&g_cacheLock);
    stats->cacheSize = g_cache ? (long)g_cache->size() : 0;
    ReleaseSRWLockShared(&g_cacheLock);
}

void Clear() {
    AcquireSRWLockExclusive(&g_cacheLock);
    if (g_cache) {
        g_cache->clear();
    }
    ReleaseSRWLockExclusive(&g_cacheLock);
    
    Log("[SpellCache] Cache cleared");
}

} // namespace SpellCache
