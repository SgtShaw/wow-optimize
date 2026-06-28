#pragma once

// ============================================================================
// Module: data_caches.h
// Description: Supporting utility functions for `data_caches.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `data_caches.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */


#include <cstdint>
#include <cstddef>

bool InitDataCaches();
void ShutdownDataCaches();

// 1. Spell History Cache
bool SpellCache_Lookup(uint32_t spellId, uint32_t* outResultHash);
void SpellCache_Store(uint32_t spellId, uint32_t resultHash);

// 2. M2 Model Init Cache
bool ModelCache_Get(uint32_t pathHash, float* farClip, float* nearClip, float* fov);
void ModelCache_Put(uint32_t pathHash, float farClip, float nearClip, float fov);

// 3. FMOD Audio Config Cache
bool AudioCache_Get(uint32_t configHash, uint32_t* result);
void AudioCache_Put(uint32_t configHash, uint32_t result);

// 4. FrameScript Opcode Cache
bool FrameScriptCache_Get(uint64_t eventKey, uint32_t* result);
void FrameScriptCache_Put(uint64_t eventKey, uint32_t result);

// 5. Event Name SSE2 Hash
uint32_t FastEventNameHash(const char* name, int len);

// 7. String Interning L2 Cache
uintptr_t StrL2_Lookup(uint64_t nameHash);
void StrL2_Store(uint64_t nameHash, uintptr_t tstringPtr);
void StrL2_Invalidate();

// 8. Combat Log Bloom Dedup
void CombatLogDedup_NewFrame();
bool CombatLogDedup_IsDuplicate(uint64_t eventFingerprint);

void ClearAllDataCaches();  // zeroes all 9 data cache arrays

// 9. Render State Batch
struct RenderStateBatch { uint32_t state; uint32_t value; };
bool RenderStateBatch_Add(uint32_t state, uint32_t value);
int RenderStateBatch_Flush(RenderStateBatch* outBatch);

// 10. Texture Decode Prefetch
void TextureDecodePrefetch(const void* blpData, size_t dataSize);