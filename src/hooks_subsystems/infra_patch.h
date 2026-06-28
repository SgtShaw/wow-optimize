#pragma once

// ============================================================================
// Module: infra_patch.h
// Description: Supporting utility functions for `infra_patch.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `infra_patch.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */


#ifndef INFRA_PATCH_H
#define INFRA_PATCH_H

#include <cstdint>
#include <cstddef>
#include <windows.h>

namespace InfraPatch {
    bool InstallAll();
    void ShutdownAll();
    void DumpStats();
}

// M1: Object pool allocator for fixed-size game objects
void* InfraPatch_PoolAlloc(int poolId, size_t objSize);
void InfraPatch_PoolFree(int poolId, void* ptr);

// M2: Texture load dedup - skip duplicate texture loads in same frame
bool InfraPatch_ShouldSkipTextureLoad(uint32_t pathHash);
void InfraPatch_MarkTextureLoaded(uint32_t pathHash);

// M3: Model vertex cache prefetch
void InfraPatch_PrefetchModelVertices(void* modelPtr, size_t vertexCount);

// M4: Spell effect batch coalesce
bool InfraPatch_ShouldCoalesceSpellEffect(uint32_t spellId, uint32_t effectIndex);

// M5: Unit field update dedup per tick
bool InfraPatch_IsFieldUpdateDuplicate(uint32_t guidLow, int fieldId, int value);

// M6: Chat channel message dedup
bool InfraPatch_IsChatMessageDuplicate(uint32_t msgHash);

// M7: Sound play dedup within time window
bool InfraPatch_ShouldSkipSoundPlay(uint32_t soundId);

// M8: Animation state transition cache
int InfraPatch_GetCachedAnimState(uint32_t modelId, int animId, int actualState);

// M9: Particle system spawn rate limiter
bool InfraPatch_ShouldSpawnParticle(uint32_t emitterId);

// M10: Nameplate text layout cache
bool InfraPatch_GetCachedNameplateLayout(uint64_t guid, float* outWidth, float* outHeight);
void InfraPatch_StoreNameplateLayout(uint64_t guid, float width, float height);

// M11-M20: Additional infrastructure APIs
bool InfraPatch_IsRaidMemberCached(uint64_t guid);
int InfraPatch_GetCachedUnitLevel(uint64_t guid, int actualLevel);
uint32_t InfraPatch_GetCachedItemQuality(uint32_t itemId, uint32_t actualQuality);
bool InfraPatch_ShouldSkipGossipText(uint32_t gossipId);
float InfraPatch_GetCachedWorldScale(float actualScale);
bool InfraPatch_IsZoneExploredCached(uint32_t zoneId);
int InfraPatch_GetCachedReputation(uint32_t factionId, int actualRep);
bool InfraPatch_ShouldCacheLootRoll(uint32_t rollId);
uint32_t InfraPatch_GetCachedMapArea(uint32_t mapId, uint32_t actualArea);
bool InfraPatch_IsQuestCompleteCached(uint32_t questId);

// M21-M30: Performance monitoring and adaptive tuning
void InfraPatch_RecordFrameTime(double ms);
double InfraPatch_GetAvgFrameTime();
bool InfraPatch_IsUnderPerformancePressure();
void InfraPatch_AdjustCacheTTL(int baseTtlMs);
int InfraPatch_GetAdaptiveTTL();
void InfraPatch_RecordAllocation(size_t bytes);
size_t InfraPatch_GetRecentAllocRate();
bool InfraPatch_ShouldReduceQuality();
void InfraPatch_SetQualityLevel(int level);
int InfraPatch_GetQualityLevel();

// M31-M40: Memory and cache management
void InfraPatch_CompactPool(int poolId);
size_t InfraPatch_GetPoolUsage(int poolId);
void InfraPatch_FlushStaleCaches();
void InfraPatch_InvalidateGuidCaches(uint64_t guid);
void InfraPatch_InvalidateItemCaches(uint32_t itemId);
void InfraPatch_InvalidateSpellCaches(uint32_t spellId);
void InfraPatch_InvalidateZoneCaches(uint32_t zoneId);
size_t InfraPatch_GetTotalCacheMemory();
void InfraPatch_SetCacheMemoryLimit(size_t limitBytes);
bool InfraPatch_IsCacheNearLimit();

// M41-M50: Advanced optimization utilities
uint32_t InfraPatch_FastJenkinsHash(const void* data, size_t len);
void InfraPatch_BatchMemcpy(void** dsts, const void** srcs, const size_t* sizes, int count);
bool InfraPatch_TryLockFreeQueuePush(volatile LONG* head, volatile LONG* tail, int mask);
int InfraPatch_CountSetBits32(uint32_t v);
int InfraPatch_NextPow2(int n);
float InfraPatch_FastInvSqrt(float x);
uint32_t InfraPatch_MurmurHash3_32(const void* key, size_t len, uint32_t seed);
void InfraPatch_SortUint32(uint32_t* arr, int n);
bool InfraPatch_BinarySearchUint32(const uint32_t* arr, int n, uint32_t key);
void InfraPatch_PrefetchRange(const void* addr, size_t len);

#endif
