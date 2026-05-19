#pragma once
// ================================================================
// Async Texture/Model Loader
// 
// ================================================================

#ifndef TEXTURE_ASYNC_H
#define TEXTURE_ASYNC_H

#include <windows.h>
#include <cstdint>

namespace TextureAsync {

// Statistics structure
struct Stats {
    volatile LONG requestsQueued;      // Total load requests queued
    volatile LONG requestsCompleted;   // Total loads completed
    volatile LONG requestsDropped;     // Requests dropped (queue full)
    volatile LONG cacheHits;           // Cache hits (already loaded)
    volatile LONG cacheMisses;         // Cache misses (need to load)
    volatile LONG queueDepth;          // Current queue depth
    double totalLoadTimeMs;            // Total load time in milliseconds
};

// Initialize the async texture/model loader
bool Init();

// Shutdown and cleanup
void Shutdown();

// Called from main thread on each frame (for stats updates)
void OnFrame(DWORD mainThreadId);

// Get current statistics
Stats GetStats();

} // namespace TextureAsync

#endif // TEXTURE_ASYNC_H
