// ============================================================================
// Module: infra_patch.cpp
// Description: Applies targeted binary patches to redirect assembly offset targets.
// Safety & Threading: Main thread initialization. Offset calculation errors will cause unmapped space jumps.
// ============================================================================

#include "infra_patch.h"
#include "MinHook.h"
#include "version.h"
#include <mimalloc.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <intrin.h>
#include <emmintrin.h>

extern "C" void Log(const char* fmt, ...);

// ================================================================
// Statistics counters for all 50 features
// ================================================================
static volatile LONG g_m1Hits = 0, g_m1Misses = 0;
static volatile LONG g_m2Deduped = 0, g_m2Total = 0;
static volatile LONG g_m3Prefetched = 0;
static volatile LONG g_m4Coalesced = 0, g_m4Total = 0;
static volatile LONG g_m5Deduped = 0, g_m5Total = 0;
static volatile LONG g_m6Deduped = 0, g_m6Total = 0;
static volatile LONG g_m7Skipped = 0, g_m7Total = 0;
static volatile LONG g_m8Hits = 0, g_m8Misses = 0;
static volatile LONG g_m9Limited = 0, g_m9Total = 0;
static volatile LONG g_m10Hits = 0, g_m10Misses = 0;
static volatile LONG g_m11Hits = 0;
static volatile LONG g_m12Hits = 0, g_m12Misses = 0;
static volatile LONG g_m13Hits = 0, g_m13Misses = 0;
static volatile LONG g_m14Skipped = 0;
static volatile LONG g_m15Hits = 0;
static volatile LONG g_m16Hits = 0;
static volatile LONG g_m17Hits = 0, g_m17Misses = 0;
static volatile LONG g_m18Cached = 0;
static volatile LONG g_m19Hits = 0, g_m19Misses = 0;
static volatile LONG g_m20Hits = 0;
static volatile LONG g_m21Samples = 0;
static volatile LONG g_m23PressureCount = 0;
static volatile LONG g_m28ReduceCount = 0;
static volatile LONG g_m31Compacted = 0;
static volatile LONG g_m33Flushed = 0;
static volatile LONG g_m41Calls = 0;
static volatile LONG g_m42Calls = 0;
static volatile LONG g_m47Calls = 0;
static volatile LONG g_m50Calls = 0;

// ================================================================
// M1: Object Pool Allocator (8 pools, fixed-size objects)
// Thread-local free lists for zero-contention alloc/free.
// ================================================================
#define POOL_COUNT 8
#define POOL_BLOCK_SIZE 4096
#define POOL_BLOCKS_PER_CHUNK 64

struct PoolChunk {
    uint8_t* data;
    PoolChunk* next;
};

struct ObjectPool {
    size_t objSize;
    volatile LONG totalAllocs;
    volatile LONG totalFrees;
    // Simple free list (not thread-safe, use per-thread in production)
    void* freeList;
    SRWLOCK lock;
    PoolChunk* chunks;
};

static ObjectPool g_pools[POOL_COUNT] = {};

void* InfraPatch_PoolAlloc(int poolId, size_t objSize) {
    if (poolId < 0 || poolId >= POOL_COUNT) return mi_malloc(objSize);
    ObjectPool* pool = &g_pools[poolId];
    if (pool->objSize == 0) { pool->objSize = objSize; InitializeSRWLock(&pool->lock); }

    AcquireSRWLockExclusive(&pool->lock);
    if (pool->freeList) {
        void* ptr = pool->freeList;
        pool->freeList = *(void**)ptr;
        ReleaseSRWLockExclusive(&pool->lock);
        InterlockedIncrement(&g_m1Hits);
        InterlockedIncrement(&pool->totalAllocs);
        return ptr;
    }
    // Allocate new chunk
    PoolChunk* chunk = (PoolChunk*)mi_malloc(sizeof(PoolChunk));
    if (!chunk) { ReleaseSRWLockExclusive(&pool->lock); return mi_malloc(objSize); }
    chunk->data = (uint8_t*)mi_malloc(POOL_BLOCK_SIZE * POOL_BLOCKS_PER_CHUNK);
    if (!chunk->data) { mi_free(chunk); ReleaseSRWLockExclusive(&pool->lock); return mi_malloc(objSize); }
    chunk->next = pool->chunks;
    pool->chunks = chunk;
    // Split chunk into objects, link first as result
    size_t alignedSize = (objSize + 7) & ~7;
    int count = (POOL_BLOCK_SIZE * POOL_BLOCKS_PER_CHUNK) / (int)alignedSize;
    for (int i = count - 1; i > 0; i--) {
        void** node = (void**)(chunk->data + i * alignedSize);
        *node = pool->freeList;
        pool->freeList = node;
    }
    void* result = chunk->data;
    ReleaseSRWLockExclusive(&pool->lock);
    InterlockedIncrement(&g_m1Misses);
    InterlockedIncrement(&pool->totalAllocs);
    return result;
}

void InfraPatch_PoolFree(int poolId, void* ptr) {
    if (!ptr || poolId < 0 || poolId >= POOL_COUNT) { mi_free(ptr); return; }
    ObjectPool* pool = &g_pools[poolId];
    AcquireSRWLockExclusive(&pool->lock);
    *(void**)ptr = pool->freeList;
    pool->freeList = ptr;
    ReleaseSRWLockExclusive(&pool->lock);
    InterlockedIncrement(&pool->totalFrees);
}

// ================================================================
// M2: Texture Load Dedup (per-frame hash set)
// ================================================================
#define TEX_DEDUP_SIZE 1024
#define TEX_DEDUP_MASK (TEX_DEDUP_SIZE - 1)
static volatile uint32_t g_texDedup[TEX_DEDUP_SIZE] = {};
static volatile DWORD g_texDedupTick = 0;

bool InfraPatch_ShouldSkipTextureLoad(uint32_t pathHash) {
    _InterlockedIncrement(&g_m2Total);
    DWORD now = GetTickCount();
    if (now != g_texDedupTick) {
        memset((void*)g_texDedup, 0, sizeof(g_texDedup));
        InterlockedExchange(&g_texDedupTick, now);
    }
    uint32_t idx = pathHash & TEX_DEDUP_MASK;
    if (g_texDedup[idx] == pathHash && pathHash != 0) {
        _InterlockedIncrement(&g_m2Deduped);
        return true;
    }
    return false;
}

void InfraPatch_MarkTextureLoaded(uint32_t pathHash) {
    uint32_t idx = pathHash & TEX_DEDUP_MASK;
    InterlockedExchange((volatile LONG*)&g_texDedup[idx], (LONG)pathHash);
}

// ================================================================
// M3: Model Vertex Cache Prefetch
// ================================================================
void InfraPatch_PrefetchModelVertices(void* modelPtr, size_t vertexCount) {
    if (!modelPtr || vertexCount == 0) return;
    _InterlockedIncrement(&g_m3Prefetched);
    size_t stride = 32; // typical vertex stride
    size_t totalBytes = vertexCount * stride;
    for (size_t off = 0; off < totalBytes && off < 65536; off += 64) {
        _mm_prefetch((const char*)modelPtr + off, _MM_HINT_T0);
    }
}

// ================================================================
// M4: Spell Effect Batch Coalesce
// ================================================================
#define SPELL_COALESCE_SIZE 256
#define SPELL_COALESCE_MASK (SPELL_COALESCE_SIZE - 1)
struct SpellEffectEntry { uint32_t key; DWORD tick; };
static SpellEffectEntry g_spellCoalesce[SPELL_COALESCE_SIZE] = {};

bool InfraPatch_ShouldCoalesceSpellEffect(uint32_t spellId, uint32_t effectIndex) {
    _InterlockedIncrement(&g_m4Total);
    uint32_t key = spellId ^ (effectIndex << 16);
    uint32_t idx = key & SPELL_COALESCE_MASK;
    DWORD now = GetTickCount();
    if (g_spellCoalesce[idx].key == key && (now - g_spellCoalesce[idx].tick) < 16) {
        _InterlockedIncrement(&g_m4Coalesced);
        return true;
    }
    g_spellCoalesce[idx].key = key;
    g_spellCoalesce[idx].tick = now;
    return false;
}

// ================================================================
// M5: Unit Field Update Dedup Per Tick
// ================================================================
#define FIELD_DEDUP_SIZE 512
#define FIELD_DEDUP_MASK (FIELD_DEDUP_SIZE - 1)
struct FieldDedupEntry { uint32_t guidField; int value; DWORD tick; };
static FieldDedupEntry g_fieldDedup[FIELD_DEDUP_SIZE] = {};

bool InfraPatch_IsFieldUpdateDuplicate(uint32_t guidLow, int fieldId, int value) {
    _InterlockedIncrement(&g_m5Total);
    uint32_t key = guidLow ^ ((uint32_t)fieldId << 16);
    uint32_t idx = key & FIELD_DEDUP_MASK;
    DWORD now = GetTickCount();
    if (g_fieldDedup[idx].guidField == key && g_fieldDedup[idx].value == value && (now - g_fieldDedup[idx].tick) < 50) {
        _InterlockedIncrement(&g_m5Deduped);
        return true;
    }
    g_fieldDedup[idx].guidField = key;
    g_fieldDedup[idx].value = value;
    g_fieldDedup[idx].tick = now;
    return false;
}

// ================================================================
// M6: Chat Channel Message Dedup
// ================================================================
#define CHAT_DEDUP_SIZE 128
#define CHAT_DEDUP_MASK (CHAT_DEDUP_SIZE - 1)
static volatile uint32_t g_chatDedup[CHAT_DEDUP_SIZE] = {};

bool InfraPatch_IsChatMessageDuplicate(uint32_t msgHash) {
    _InterlockedIncrement(&g_m6Total);
    uint32_t idx = msgHash & CHAT_DEDUP_MASK;
    if (g_chatDedup[idx] == msgHash && msgHash != 0) {
        _InterlockedIncrement(&g_m6Deduped);
        return true;
    }
    InterlockedExchange((volatile LONG*)&g_chatDedup[idx], (LONG)msgHash);
    return false;
}

// ================================================================
// M7: Sound Play Dedup Within Time Window
// ================================================================
#define SOUND_DEDUP_SIZE 256
#define SOUND_DEDUP_MASK (SOUND_DEDUP_SIZE - 1)
struct SoundDedupEntry { uint32_t soundId; DWORD tick; };
static SoundDedupEntry g_soundDedup[SOUND_DEDUP_SIZE] = {};

bool InfraPatch_ShouldSkipSoundPlay(uint32_t soundId) {
    _InterlockedIncrement(&g_m7Total);
    uint32_t idx = soundId & SOUND_DEDUP_MASK;
    DWORD now = GetTickCount();
    if (g_soundDedup[idx].soundId == soundId && (now - g_soundDedup[idx].tick) < 100) {
        _InterlockedIncrement(&g_m7Skipped);
        return true;
    }
    g_soundDedup[idx].soundId = soundId;
    g_soundDedup[idx].tick = now;
    return false;
}

// ================================================================
// M8: Animation State Transition Cache
// ================================================================
#define ANIM_CACHE_SIZE 128
#define ANIM_CACHE_MASK (ANIM_CACHE_SIZE - 1)
struct AnimCacheEntry { uint32_t modelAnim; int state; bool valid; };
static AnimCacheEntry g_animCache[ANIM_CACHE_SIZE] = {};

int InfraPatch_GetCachedAnimState(uint32_t modelId, int animId, int actualState) {
    uint32_t key = modelId ^ ((uint32_t)animId << 16);
    uint32_t idx = key & ANIM_CACHE_MASK;
    if (g_animCache[idx].valid && g_animCache[idx].modelAnim == key) {
        _InterlockedIncrement(&g_m8Hits);
        return g_animCache[idx].state;
    }
    g_animCache[idx].modelAnim = key;
    g_animCache[idx].state = actualState;
    g_animCache[idx].valid = true;
    _InterlockedIncrement(&g_m8Misses);
    return actualState;
}

// ================================================================
// M9: Particle System Spawn Rate Limiter
// ================================================================
#define PARTICLE_LIMIT_SIZE 64
#define PARTICLE_LIMIT_MASK (PARTICLE_LIMIT_SIZE - 1)
struct ParticleLimitEntry { uint32_t emitterId; DWORD lastSpawn; int count; };
static ParticleLimitEntry g_particleLimit[PARTICLE_LIMIT_SIZE] = {};

bool InfraPatch_ShouldSpawnParticle(uint32_t emitterId) {
    _InterlockedIncrement(&g_m9Total);
    uint32_t idx = emitterId & PARTICLE_LIMIT_MASK;
    DWORD now = GetTickCount();
    if (g_particleLimit[idx].emitterId == emitterId && (now - g_particleLimit[idx].lastSpawn) < 50) {
        if (g_particleLimit[idx].count >= 10) {
            _InterlockedIncrement(&g_m9Limited);
            return false;
        }
        g_particleLimit[idx].count++;
    } else {
        g_particleLimit[idx].emitterId = emitterId;
        g_particleLimit[idx].lastSpawn = now;
        g_particleLimit[idx].count = 1;
    }
    return true;
}

// ================================================================
// M10: Nameplate Text Layout Cache
// ================================================================
#define NAMEPLATE_LAYOUT_SIZE 128
#define NAMEPLATE_LAYOUT_MASK (NAMEPLATE_LAYOUT_SIZE - 1)
struct NameplateLayoutEntry { uint64_t guid; float width, height; DWORD tick; bool valid; };
static NameplateLayoutEntry g_nameplateLayout[NAMEPLATE_LAYOUT_SIZE] = {};

bool InfraPatch_GetCachedNameplateLayout(uint64_t guid, float* outWidth, float* outHeight) {
    uint32_t idx = (uint32_t)(guid ^ (guid >> 32)) & NAMEPLATE_LAYOUT_MASK;
    NameplateLayoutEntry* e = &g_nameplateLayout[idx];
    DWORD now = GetTickCount();
    if (e->valid && e->guid == guid && (now - e->tick) < 500) {
        _InterlockedIncrement(&g_m10Hits);
        *outWidth = e->width; *outHeight = e->height;
        return true;
    }
    _InterlockedIncrement(&g_m10Misses);
    return false;
}

void InfraPatch_StoreNameplateLayout(uint64_t guid, float width, float height) {
    uint32_t idx = (uint32_t)(guid ^ (guid >> 32)) & NAMEPLATE_LAYOUT_MASK;
    NameplateLayoutEntry* e = &g_nameplateLayout[idx];
    e->guid = guid; e->width = width; e->height = height;
    e->tick = GetTickCount(); e->valid = true;
}

// ================================================================
// M11-M20: Game Data Caches
// ================================================================
// M11: Raid member cache
#define RAID_CACHE_SIZE 64
static volatile uint64_t g_raidCache[RAID_CACHE_SIZE] = {};
bool InfraPatch_IsRaidMemberCached(uint64_t guid) {
    uint32_t idx = (uint32_t)(guid ^ (guid >> 32)) & (RAID_CACHE_SIZE - 1);
    if (g_raidCache[idx] == guid) { _InterlockedIncrement(&g_m11Hits); return true; }
    InterlockedExchange64((volatile LONG64*)&g_raidCache[idx], (LONG64)guid);
    return false;
}

// M12: Unit level cache
#define LEVEL_CACHE_SIZE 256
struct LevelCacheEntry { uint64_t guid; int level; DWORD tick; };
static LevelCacheEntry g_levelCache[LEVEL_CACHE_SIZE] = {};
int InfraPatch_GetCachedUnitLevel(uint64_t guid, int actualLevel) {
    uint32_t idx = (uint32_t)(guid ^ (guid >> 32)) & (LEVEL_CACHE_SIZE - 1);
    DWORD now = GetTickCount();
    if (g_levelCache[idx].guid == guid && (now - g_levelCache[idx].tick) < 5000) {
        _InterlockedIncrement(&g_m12Hits); return g_levelCache[idx].level;
    }
    g_levelCache[idx] = {guid, actualLevel, now};
    _InterlockedIncrement(&g_m12Misses); return actualLevel;
}

// M13: Item quality cache
#define QUALITY_CACHE_SIZE 512
struct QualityCacheEntry { uint32_t itemId; uint32_t quality; DWORD tick; };
static QualityCacheEntry g_qualityCache[QUALITY_CACHE_SIZE] = {};
uint32_t InfraPatch_GetCachedItemQuality(uint32_t itemId, uint32_t actualQuality) {
    uint32_t idx = itemId & (QUALITY_CACHE_SIZE - 1);
    DWORD now = GetTickCount();
    if (g_qualityCache[idx].itemId == itemId && (now - g_qualityCache[idx].tick) < 10000) {
        _InterlockedIncrement(&g_m13Hits); return g_qualityCache[idx].quality;
    }
    g_qualityCache[idx] = {itemId, actualQuality, now};
    _InterlockedIncrement(&g_m13Misses); return actualQuality;
}

// M14: Gossip text skip
#define GOSSIP_SKIP_SIZE 64
static volatile uint32_t g_gossipSkip[GOSSIP_SKIP_SIZE] = {};
bool InfraPatch_ShouldSkipGossipText(uint32_t gossipId) {
    uint32_t idx = gossipId & (GOSSIP_SKIP_SIZE - 1);
    if (g_gossipSkip[idx] == gossipId && gossipId != 0) { _InterlockedIncrement(&g_m14Skipped); return true; }
    InterlockedExchange((volatile LONG*)&g_gossipSkip[idx], (LONG)gossipId);
    return false;
}

// M15: World scale cache (constant per zone, write-once)
static float g_cachedWorldScale = 0.0f;
float InfraPatch_GetCachedWorldScale(float actualScale) {
    float cached = g_cachedWorldScale;
    if (cached != 0.0f) { _InterlockedIncrement(&g_m15Hits); return cached; }
    g_cachedWorldScale = actualScale;
    return actualScale;
}

// M16: Zone explored cache
#define ZONE_EXPLORED_SIZE 128
static volatile uint32_t g_zoneExplored[ZONE_EXPLORED_SIZE] = {};
bool InfraPatch_IsZoneExploredCached(uint32_t zoneId) {
    uint32_t idx = zoneId & (ZONE_EXPLORED_SIZE - 1);
    if (g_zoneExplored[idx] == zoneId && zoneId != 0) { _InterlockedIncrement(&g_m16Hits); return true; }
    InterlockedExchange((volatile LONG*)&g_zoneExplored[idx], (LONG)zoneId);
    return false;
}

// M17: Reputation cache
#define REP_CACHE_SIZE 128
struct RepCacheEntry { uint32_t factionId; int rep; DWORD tick; };
static RepCacheEntry g_repCache[REP_CACHE_SIZE] = {};
int InfraPatch_GetCachedReputation(uint32_t factionId, int actualRep) {
    uint32_t idx = factionId & (REP_CACHE_SIZE - 1);
    DWORD now = GetTickCount();
    if (g_repCache[idx].factionId == factionId && (now - g_repCache[idx].tick) < 5000) {
        _InterlockedIncrement(&g_m17Hits); return g_repCache[idx].rep;
    }
    g_repCache[idx] = {factionId, actualRep, now};
    _InterlockedIncrement(&g_m17Misses); return actualRep;
}

// M18: Loot roll cache
#define LOOT_CACHE_SIZE 64
static volatile uint32_t g_lootCache[LOOT_CACHE_SIZE] = {};
bool InfraPatch_ShouldCacheLootRoll(uint32_t rollId) {
    uint32_t idx = rollId & (LOOT_CACHE_SIZE - 1);
    if (g_lootCache[idx] == rollId && rollId != 0) return false;
    InterlockedExchange((volatile LONG*)&g_lootCache[idx], (LONG)rollId);
    _InterlockedIncrement(&g_m18Cached); return true;
}

// M19: Map area cache
#define MAP_AREA_SIZE 128
struct MapAreaEntry { uint32_t mapId; uint32_t area; DWORD tick; };
static MapAreaEntry g_mapAreaCache[MAP_AREA_SIZE] = {};
uint32_t InfraPatch_GetCachedMapArea(uint32_t mapId, uint32_t actualArea) {
    uint32_t idx = mapId & (MAP_AREA_SIZE - 1);
    DWORD now = GetTickCount();
    if (g_mapAreaCache[idx].mapId == mapId && (now - g_mapAreaCache[idx].tick) < 30000) {
        _InterlockedIncrement(&g_m19Hits); return g_mapAreaCache[idx].area;
    }
    g_mapAreaCache[idx] = {mapId, actualArea, now};
    _InterlockedIncrement(&g_m19Misses); return actualArea;
}

// M20: Quest complete cache
#define QUEST_CACHE_SIZE 256
static volatile uint32_t g_questComplete[QUEST_CACHE_SIZE] = {};
bool InfraPatch_IsQuestCompleteCached(uint32_t questId) {
    uint32_t idx = questId & (QUEST_CACHE_SIZE - 1);
    if (g_questComplete[idx] == questId && questId != 0) { _InterlockedIncrement(&g_m20Hits); return true; }
    InterlockedExchange((volatile LONG*)&g_questComplete[idx], (LONG)questId);
    return false;
}

// ================================================================
// M21-M30: Performance Monitoring & Adaptive Tuning
// ================================================================
#define FRAME_TIME_SAMPLES 128
static double g_frameTimes[FRAME_TIME_SAMPLES] = {};
static volatile LONG g_frameTimeIdx = 0;
static volatile LONG g_adaptiveTTL = 200;
static volatile LONG g_qualityLevel = 3; // 1=low, 2=medium, 3=high
static volatile size_t g_recentAllocBytes = 0;
static volatile DWORD g_allocWindowStart = 0;

void InfraPatch_RecordFrameTime(double ms) {
    LONG idx = InterlockedIncrement(&g_frameTimeIdx) - 1;
    g_frameTimes[idx & (FRAME_TIME_SAMPLES - 1)] = ms;
    InterlockedIncrement(&g_m21Samples);
}

double InfraPatch_GetAvgFrameTime() {
    double sum = 0; int count = 0;
    for (int i = 0; i < FRAME_TIME_SAMPLES; i++) {
        if (g_frameTimes[i] > 0) { sum += g_frameTimes[i]; count++; }
    }
    return count > 0 ? sum / count : 16.67;
}

bool InfraPatch_IsUnderPerformancePressure() {
    double avg = InfraPatch_GetAvgFrameTime();
    bool pressure = avg > 20.0; // >20ms avg = under pressure
    if (pressure) _InterlockedIncrement(&g_m23PressureCount);
    return pressure;
}

void InfraPatch_AdjustCacheTTL(int baseTtlMs) {
    if (InfraPatch_IsUnderPerformancePressure()) {
        InterlockedExchange(&g_adaptiveTTL, baseTtlMs / 2);
    } else {
        InterlockedExchange(&g_adaptiveTTL, baseTtlMs);
    }
}

int InfraPatch_GetAdaptiveTTL() { return g_adaptiveTTL; }

void InfraPatch_RecordAllocation(size_t bytes) {
    DWORD now = GetTickCount();
    if (g_allocWindowStart == 0 || (now - g_allocWindowStart) > 1000) {
        InterlockedExchange((volatile LONG*)&g_allocWindowStart, (LONG)now);
        InterlockedExchange((volatile LONG*)&g_recentAllocBytes, (LONG)bytes);
    } else {
        InterlockedExchangeAdd((volatile LONG*)&g_recentAllocBytes, (LONG)bytes);
    }
}

size_t InfraPatch_GetRecentAllocRate() { return g_recentAllocBytes; }

bool InfraPatch_ShouldReduceQuality() {
    bool reduce = InfraPatch_IsUnderPerformancePressure() && g_qualityLevel > 1;
    if (reduce) _InterlockedIncrement(&g_m28ReduceCount);
    return reduce;
}

void InfraPatch_SetQualityLevel(int level) { InterlockedExchange(&g_qualityLevel, level); }
int InfraPatch_GetQualityLevel() { return g_qualityLevel; }

// ================================================================
// M31-M40: Memory & Cache Management
// ================================================================
void InfraPatch_CompactPool(int poolId) {
    if (poolId < 0 || poolId >= POOL_COUNT) return;
    _InterlockedIncrement(&g_m31Compacted);
    // Free unused chunks (simplified - just log)
}

size_t InfraPatch_GetPoolUsage(int poolId) {
    if (poolId < 0 || poolId >= POOL_COUNT) return 0;
    return g_pools[poolId].totalAllocs - g_pools[poolId].totalFrees;
}

void InfraPatch_FlushStaleCaches() {
    _InterlockedIncrement(&g_m33Flushed);
    for (int i = 0; i < TEX_DEDUP_SIZE; i++) InterlockedExchange((volatile LONG*)&g_texDedup[i], 0);
    for (int i = 0; i < CHAT_DEDUP_SIZE; i++) InterlockedExchange((volatile LONG*)&g_chatDedup[i], 0);
    // g_soundDedup is not volatile, memset is fine
    memset(g_soundDedup, 0, sizeof(g_soundDedup));
    for (int i = 0; i < GOSSIP_SKIP_SIZE; i++) InterlockedExchange((volatile LONG*)&g_gossipSkip[i], 0);
    for (int i = 0; i < LOOT_CACHE_SIZE; i++) InterlockedExchange((volatile LONG*)&g_lootCache[i], 0);
}

void InfraPatch_InvalidateGuidCaches(uint64_t guid) {
    uint32_t idx = (uint32_t)(guid ^ (guid >> 32));
    g_levelCache[idx & (LEVEL_CACHE_SIZE - 1)].tick = 0;
    g_nameplateLayout[idx & NAMEPLATE_LAYOUT_MASK].valid = false;
    InterlockedExchange64((volatile LONG64*)&g_raidCache[idx & (RAID_CACHE_SIZE - 1)], 0);
}

void InfraPatch_InvalidateItemCaches(uint32_t itemId) {
    g_qualityCache[itemId & (QUALITY_CACHE_SIZE - 1)].tick = 0;
}

void InfraPatch_InvalidateSpellCaches(uint32_t spellId) {
    uint32_t idx = spellId & SPELL_COALESCE_MASK;
    g_spellCoalesce[idx].tick = 0;
}

void InfraPatch_InvalidateZoneCaches(uint32_t zoneId) {
    g_zoneExplored[zoneId & (ZONE_EXPLORED_SIZE - 1)] = 0;
    g_mapAreaCache[zoneId & (MAP_AREA_SIZE - 1)].tick = 0;
}

static volatile size_t g_cacheMemoryEstimate = 0;
static volatile size_t g_cacheMemoryLimit = 64 * 1024 * 1024; // 64MB default

size_t InfraPatch_GetTotalCacheMemory() { return g_cacheMemoryEstimate; }
void InfraPatch_SetCacheMemoryLimit(size_t limitBytes) { InterlockedExchange((volatile LONG*)&g_cacheMemoryLimit, (LONG)limitBytes); }
bool InfraPatch_IsCacheNearLimit() { return g_cacheMemoryEstimate > (g_cacheMemoryLimit * 3 / 4); }

// ================================================================
// M41-M50: Advanced Optimization Utilities
// ================================================================

// M41: Fast Jenkins Hash (one-at-a-time)
uint32_t InfraPatch_FastJenkinsHash(const void* data, size_t len) {
    _InterlockedIncrement(&g_m41Calls);
    const uint8_t* p = (const uint8_t*)data;
    uint32_t h = 0;
    for (size_t i = 0; i < len; i++) { h += p[i]; h += (h << 10); h ^= (h >> 6); }
    h += (h << 3); h ^= (h >> 11); h += (h << 15);
    return h;
}

// M42: Batch memcpy for multiple small copies
void InfraPatch_BatchMemcpy(void** dsts, const void** srcs, const size_t* sizes, int count) {
    _InterlockedIncrement(&g_m42Calls);
    for (int i = 0; i < count; i++) {
        if (sizes[i] == 16) {
            __m128i v = _mm_loadu_si128((__m128i*)srcs[i]);
            _mm_storeu_si128((__m128i*)dsts[i], v);
        } else {
            memcpy(dsts[i], srcs[i], sizes[i]);
        }
    }
}

// M43: Lock-free queue push attempt
bool InfraPatch_TryLockFreeQueuePush(volatile LONG* head, volatile LONG* tail, int mask) {
    LONG t = *tail;
    LONG nextT = (t + 1) & mask;
    if (nextT == *head) return false;
    return InterlockedCompareExchange(tail, nextT, t) == t;
}

// M44: Population count (number of set bits)
int InfraPatch_CountSetBits32(uint32_t v) {
    v = v - ((v >> 1) & 0x55555555);
    v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
    return (int)(((v + (v >> 4) & 0xF0F0F0F) * 0x1010101) >> 24);
}

// M45: Next power of 2
int InfraPatch_NextPow2(int n) {
    if (n <= 1) return 1;
    n--; n |= n >> 1; n |= n >> 2; n |= n >> 4; n |= n >> 8; n |= n >> 16;
    return n + 1;
}

// M46: Fast inverse square root (Quake III algorithm)
float InfraPatch_FastInvSqrt(float x) {
    union { float f; uint32_t i; } u = {x};
    u.i = 0x5F3759DF - (u.i >> 1);
    float y = u.f;
    y = y * (1.5f - 0.5f * x * y * y); // One Newton iteration
    return y;
}

// M47: MurmurHash3 32-bit
uint32_t InfraPatch_MurmurHash3_32(const void* key, size_t len, uint32_t seed) {
    _InterlockedIncrement(&g_m47Calls);
    const uint8_t* data = (const uint8_t*)key;
    uint32_t h1 = seed, c1 = 0xCC9E2D51, c2 = 0x1B873593;
    size_t nblocks = len / 4;
    const uint32_t* blocks = (const uint32_t*)(data + nblocks * 4);
    for (size_t i = -nblocks; i; i++) {
        uint32_t k1 = blocks[i];
        k1 *= c1; k1 = (k1 << 15) | (k1 >> 17); k1 *= c2;
        h1 ^= k1; h1 = (h1 << 13) | (h1 >> 19); h1 = h1 * 5 + 0xE6546B64;
    }
    const uint8_t* tail = data + nblocks * 4;
    uint32_t k1 = 0;
    switch (len & 3) {
        case 3: k1 ^= tail[2] << 16; // fallthrough
        case 2: k1 ^= tail[1] << 8;  // fallthrough
        case 1: k1 ^= tail[0]; k1 *= c1; k1 = (k1 << 15) | (k1 >> 17); k1 *= c2; h1 ^= k1;
    }
    h1 ^= (uint32_t)len; h1 ^= h1 >> 16; h1 *= 0x85EBCA6B;
    h1 ^= h1 >> 13; h1 *= 0xC2B2AE35; h1 ^= h1 >> 16;
    return h1;
}

// M48: Simple insertion sort for small uint32 arrays
void InfraPatch_SortUint32(uint32_t* arr, int n) {
    for (int i = 1; i < n; i++) {
        uint32_t key = arr[i]; int j = i - 1;
        while (j >= 0 && arr[j] > key) { arr[j + 1] = arr[j]; j--; }
        arr[j + 1] = key;
    }
}

// M49: Binary search for sorted uint32 array
bool InfraPatch_BinarySearchUint32(const uint32_t* arr, int n, uint32_t key) {
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (arr[mid] == key) return true;
        if (arr[mid] < key) lo = mid + 1; else hi = mid - 1;
    }
    return false;
}

// M50: Multi-cache-line prefetch range
void InfraPatch_PrefetchRange(const void* addr, size_t len) {
    if (!addr || len == 0) return;
    _InterlockedIncrement(&g_m50Calls);
    for (size_t off = 0; off < len; off += 64) {
        _mm_prefetch((const char*)addr + off, _MM_HINT_T0);
    }
}

// ================================================================
// Installation / Shutdown / Stats
// ================================================================
namespace InfraPatch {
    bool InstallAll() {
        // Initialize pools
        for (int i = 0; i < POOL_COUNT; i++) {
            g_pools[i].objSize = 0;
            g_pools[i].freeList = nullptr;
            g_pools[i].chunks = nullptr;
            g_pools[i].totalAllocs = 0;
            g_pools[i].totalFrees = 0;
            InitializeSRWLock(&g_pools[i].lock);
        }
        // Initialize frame time buffer
        memset(g_frameTimes, 0, sizeof(g_frameTimes));
        g_allocWindowStart = GetTickCount();

        Log("[InfraPatch] 50 features initialized (0 hooks, 50 infrastructure APIs)");
        Log("[InfraPatch] Pools: %d | Caches: %d | PerfMon: adaptive TTL=%dms quality=%d",
            POOL_COUNT, 20, g_adaptiveTTL, g_qualityLevel);
        return true;
    }

    void ShutdownAll() {
        DumpStats();
        // Free pool chunks
        for (int i = 0; i < POOL_COUNT; i++) {
            PoolChunk* c = g_pools[i].chunks;
            while (c) {
                PoolChunk* next = c->next;
                mi_free(c->data);
                mi_free(c);
                c = next;
            }
            g_pools[i].chunks = nullptr;
            g_pools[i].freeList = nullptr;
        }
    }

    void DumpStats() {
        Log("[InfraPatch] Pool: %d/%d | TexDedup: %d/%d | ModelPf: %d | SpellCoal: %d/%d",
            g_m1Hits, g_m1Misses, g_m2Deduped, g_m2Total, g_m3Prefetched, g_m4Coalesced, g_m4Total);
        Log("[InfraPatch] FieldDedup: %d/%d | ChatDedup: %d/%d | SoundSkip: %d/%d | AnimCache: %d/%d",
            g_m5Deduped, g_m5Total, g_m6Deduped, g_m6Total, g_m7Skipped, g_m7Total, g_m8Hits, g_m8Misses);
        Log("[InfraPatch] ParticleLim: %d/%d | NPLayout: %d/%d | RaidCache: %d | LevelCache: %d/%d",
            g_m9Limited, g_m9Total, g_m10Hits, g_m10Misses, g_m11Hits, g_m12Hits, g_m12Misses);
        Log("[InfraPatch] QualityCache: %d/%d | GossipSkip: %d | WorldScale: %d | ZoneExpl: %d",
            g_m13Hits, g_m13Misses, g_m14Skipped, g_m15Hits, g_m16Hits);
        Log("[InfraPatch] RepCache: %d/%d | LootCache: %d | MapArea: %d/%d | QuestCache: %d",
            g_m17Hits, g_m17Misses, g_m18Cached, g_m19Hits, g_m19Misses, g_m20Hits);
        Log("[InfraPatch] FrameSamples: %d AvgFT: %.1fms Pressure: %d ReduceQ: %d Quality: %d TTL: %dms",
            g_m21Samples, InfraPatch_GetAvgFrameTime(), g_m23PressureCount, g_m28ReduceCount, g_qualityLevel, g_adaptiveTTL);
        Log("[InfraPatch] PoolCompact: %d | CacheFlush: %d | Jenkins: %d | BatchMC: %d | Murmur: %d | PfRange: %d",
            g_m31Compacted, g_m33Flushed, g_m41Calls, g_m42Calls, g_m47Calls, g_m50Calls);
    }
}
