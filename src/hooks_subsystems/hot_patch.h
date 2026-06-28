#pragma once

// ============================================================================
// Module: hot_patch.h
// Description: Supporting utility functions for `hot_patch.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `hot_patch.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */


#ifndef HOT_PATCH_H
#define HOT_PATCH_H

#include <cstdint>
#include <cstddef>
#include <windows.h>

namespace HotPatch {
    bool InstallAll();
    void ShutdownAll();
    void DumpStats();
}

// N6: Cleanup sibling prefetch
void HotPatch_PrefetchCleanupSibling(void* block);

// N7: Lua error bypass check
bool HotPatch_ShouldSkipLuaError(const char* fmt);

// N9: Cached string hash (FNV-1a)
uint32_t HotPatch_CachedStringHash(const char* str, size_t len);

// N10: CS owner thread cache
bool HotPatch_IsCsOwnedByCurrentThread(LPCRITICAL_SECTION cs);

// N11: MPQ block table index cache
bool HotPatch_CacheBlockLookup(uint32_t fileHash, uint32_t* outBlockIndex, uint32_t* outOffset);
void HotPatch_StoreBlockLookup(uint32_t fileHash, uint32_t blockIndex, uint32_t offset);

// N12: Render state coalescing
bool HotPatch_ShouldCoalesceRenderState(DWORD stateType, DWORD value);

// N13: Event dispatch dedup
bool HotPatch_ShouldDedupEvent(const char* eventName);

// N14: Unit aura count cache
int HotPatch_GetCachedAuraCount(uint32_t guidLow, int actualCount);

// N15: Spell cooldown timer cache
int HotPatch_GetCachedCooldownRemaining(uint32_t spellId, float cooldownEnd, float currentTime);

// N16: Chat message format cache
const char* HotPatch_GetCachedChatMessage(const char* fmt, uint32_t argHash, const char* fallbackMsg);

// N17: Minimap icon position cache
bool HotPatch_GetCachedMinimapPos(int iconIndex, float zoom, float rotation, float* outX, float* outY);
void HotPatch_StoreMinimapPos(int iconIndex, float zoom, float rotation, float screenX, float screenY);

// N18: Action bar button state cache
bool HotPatch_ButtonStateChanged(int slotIndex, uint8_t newState);
void HotPatch_InvalidateButtonCache();

// N19: Combat text float cache
bool HotPatch_GetCachedCombatTextPos(float worldX, float worldY, float worldZ, float* outScreenX, float* outScreenY);
void HotPatch_StoreCombatTextPos(float worldX, float worldY, float worldZ, float screenX, float screenY);

// N20: Inventory slot item cache
uint32_t HotPatch_GetCachedInvItem(int bagId, int slotIndex, uint32_t actualItemId);
void HotPatch_InvalidateInvCache();

#endif