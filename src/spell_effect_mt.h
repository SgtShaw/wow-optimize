// ================================================================
// Multithreaded Spell Effect Renderer — Header
// WoW 3.3.5a build 12340
//
// WHAT: Offloads spell visual effect rendering to worker threads
//       to eliminate FPS drops in raids with hundreds of effects.
//
// WHY:  In 25-man raids, spell effect rendering causes FPS drops
//       from 60 to 20-30 FPS due to synchronous main thread processing.
//
// HOW:  1. Hook spell effect rendering function (IDA Pro analysis)
//       2. Queue effect requests to lock-free ring buffer (8192 entries)
//       3. Worker threads (4 threads) render effects in parallel
//       4. Main thread applies rendered results during OnFrame()
//       5. LRU cache (4096 entries) accelerates common effects
//
// PERFORMANCE TARGETS:
//   - 40-60% main thread CPU reduction in raids
//   - FPS improvement from 20-30 to 45-55 in heavy combat
//   - Queue utilization <50%
//   - Cache hit rate >70%
//
// ================================================================

#pragma once

#include <windows.h>
#include <cstdint>

namespace SpellEffectMT {

// ================================================================
// Effect Request Structure (Main Thread → Worker Thread)
// ================================================================
struct EffectRequest {
    // Effect identification
    void* effectPtr;           // +0x00: Pointer to WoW effect object
    uint32_t effectID;         // +0x04: Spell effect ID (for cache key)
    uint32_t animationFrame;   // +0x08: Current animation frame
    
    // Spatial data
    float position[3];         // +0x0C: World position (x, y, z)
    float rotation[4];         // +0x18: Quaternion rotation (x, y, z, w)
    
    // Rendering parameters
    uint32_t textureID;        // +0x28: Texture identifier
    float scale;               // +0x2C: Effect scale multiplier
    uint32_t color;            // +0x30: ARGB color
    uint32_t flags;            // +0x34: Rendering flags
    
    // Metadata
    DWORD timestamp;           // +0x38: Request timestamp (QPC)
};
// Total size: 0x3C (60 bytes)

// ================================================================
// Effect Result Structure (Worker Thread → Main Thread)
// ================================================================
struct EffectResult {
    // Effect identification (for matching with request)
    void* effectPtr;           // +0x00: Original effect pointer
    uint32_t effectID;         // +0x04: Spell effect ID
    uint32_t animationFrame;   // +0x08: Animation frame
    
    // Rendered data (stored inline, not as pointer)
    float position_result[3];  // +0x0C: Position result (x, y, z)
    float rotation_result[3];  // +0x18: Rotation result (x, y, z)
    
    // Performance metrics
    DWORD processingTimeUs;    // +0x24: Processing time in microseconds
    DWORD timestamp;           // +0x28: Completion timestamp (QPC)
};
// Total size: 0x2C (44 bytes)

// ================================================================
// Statistics Structure
// ================================================================
struct Stats {
    // Throughput metrics
    volatile LONG effectsQueued;
    volatile LONG effectsProcessed;
    volatile LONG effectsDropped;
    volatile LONG cacheHits;
    volatile LONG cacheMisses;
    
    // Performance metrics
    volatile LONG effectsPerSecond;
    volatile LONG avgProcessingTimeUs;
    volatile LONG queueUtilizationPct;
    volatile LONG workerCpuUsagePct;
    volatile LONG mainThreadTimeSavedMs;
    
    // FPS metrics
    volatile LONG fpsBeforeOptimization;
    volatile LONG fpsAfterOptimization;
    volatile LONG fpsImprovementPct;
    
    // Queue depth metrics
    volatile LONG inputQueueDepth;
    volatile LONG outputQueueDepth;
    volatile LONG maxInputQueueDepth;
    volatile LONG maxOutputQueueDepth;
    
    // Error metrics
    volatile LONG exceptionsHandled;
    volatile LONG synchronousFallbacks;
};

// ================================================================
// Public API
// ================================================================

// Initialize the spell effect renderer
// Returns true on success, false on failure
bool Init();

// Shutdown the spell effect renderer
// Waits for worker threads to complete (5 second timeout)
void Shutdown();

// Process rendered effects and apply to rendering
// Must be called once per frame from main thread only
// mainThreadId: ID of the main rendering thread
void OnFrame(DWORD mainThreadId);

// Get performance statistics
// Returns current statistics snapshot
Stats GetStats();

} // namespace SpellEffectMT
