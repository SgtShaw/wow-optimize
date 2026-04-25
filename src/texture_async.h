#pragma once
// ================================================================
// Async Texture/Model Loader for wow_optimize.dll — build 12340
// 
// WHAT: Offloads texture and model loading from main thread to
//       worker thread pool using lock-free queue.
// WHY:  Texture/model loading causes 80-90% of loading stutters
//       and freezes during teleports/zone changes.
// HOW:  1. Hook sub_619330 (texture loading function)
//       2. Queue load requests to worker thread pool (2-4 threads)
//       3. Load textures asynchronously from MPQ files
//       4. Cache loaded texture data in memory
//       5. Return cached data when ready on main thread
// ADDRESSES:
//   - sub_619330: 0x00619330 (texture loading with .blp format)
// STATUS: Experimental — colossal-scale optimization
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
