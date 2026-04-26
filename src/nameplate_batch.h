#pragma once
// ================================================================
// Multithreaded Nameplate Renderer for wow_optimize.dll — build 12340
// 
// WHAT: Offloads nameplate rendering from main thread to dedicated
//       worker threads using lock-free queues.
// WHY:  Nameplate rendering consumes 40-60% of main thread CPU in
//       25-man raids, causing FPS drops from 60+ to 20-30 FPS.
// HOW:  1. Hook nameplate update functions (health, text, color, visibility)
//       2. Copy nameplate data to lock-free input queue (4096 entries)
//       3. Worker threads (2) process asynchronously
//       4. Results queued back to main thread for UI application
//       5. Priority system: Target > Focus > Nearby > Distant
// ADDRESSES:
//   - sub_6B2A40: 0x006B2A40 (nameplate health bar update)
//   - sub_6B1F80: 0x006B1F80 (nameplate text rendering)
//   - sub_6B3120: 0x006B3120 (nameplate color calculation)
//   - sub_6B2E60: 0x006B2E60 (nameplate visibility determination)
// STATUS: Initial implementation - v3.5.14
// ================================================================

#ifndef NAMEPLATE_BATCH_H
#define NAMEPLATE_BATCH_H

#include <windows.h>
#include <cstdint>

namespace NameplateMT {

// ================================================================
// Task and Result Type Enumerations
// ================================================================
enum TaskType {
    TASK_HEALTH_UPDATE = 1,    // Health bar color and percentage
    TASK_TEXT_UPDATE = 2,      // Name, level, guild text formatting
    TASK_COLOR_UPDATE = 3,     // Class color, threat color calculation
    TASK_VISIBILITY_UPDATE = 4 // Show/hide and alpha calculation
};

enum NameplatePriority {
    PRIORITY_TARGET = 0,    // Player's current target - immediate processing
    PRIORITY_FOCUS = 1,     // Player's focus target - high priority
    PRIORITY_NEARBY = 2,    // Nameplates within 20 yards - normal priority
    PRIORITY_DISTANT = 3    // Nameplates beyond 20 yards - batch processing
};

// ================================================================
// Core Data Structures
// ================================================================

// Nameplate processing task (queued by main thread)
struct NameplateTask {
    TaskType type;              // Type of processing required
    void* nameplate;           // Nameplate UI object pointer
    DWORD priority;            // Processing priority (0=highest)
    DWORD timestamp;           // Task creation timestamp
    
    // Task-specific data (union for memory efficiency)
    union {
        struct {               // Health update data
            int healthCurrent;
            int healthMax;
        };
        struct {               // Text update data
            char unitName[64];
            char guildName[64];
            int unitLevel;
        };
        struct {               // Color update data
            DWORD classColor;
            DWORD threatColor;
            float threatLevel;
        };
        struct {               // Visibility data
            DWORD flags;
            float distance;
        };
    };
};

// Nameplate processing result (queued back to main thread)
struct NameplateResult {
    void* nameplate;           // Target nameplate for result
    TaskType type;             // Type of result
    DWORD processingTimeUs;    // Time spent processing (for statistics)
    
    // Result-specific data
    union {
        struct {               // Processed health data
            DWORD healthBarColor;
            float healthPercent;
        };
        struct {               // Processed text data
            char formattedText[128];
            DWORD textColor;
        };
        struct {               // Processed color data
            DWORD finalColor;
            DWORD borderColor;
        };
        struct {               // Processed visibility
            BOOL shouldShow;
            float alpha;
        };
    };
};

// Statistics structure
struct Stats {
    // Core processing metrics
    volatile LONG tasksQueued;           // Total tasks queued
    volatile LONG tasksProcessed;        // Total tasks processed
    volatile LONG tasksDropped;          // Tasks dropped (overflow)
    volatile LONG resultsProcessed;      // Results applied to UI
    volatile LONG exceptionsHandled;     // Exceptions caught and handled
    
    // Performance metrics
    volatile LONG nameplatesPerSecond;   // Nameplates processed per second
    volatile LONG avgProcessingTimeUs;   // Average processing time (microseconds)
    volatile LONG queueUtilizationPct;   // Queue utilization percentage
    volatile LONG workerCpuUsagePct;     // Worker thread CPU usage
    volatile LONG mainThreadTimeSavedMs; // Main thread time saved per frame
    
    // FPS tracking
    volatile LONG fpsBeforeOptimization; // FPS before optimization
    volatile LONG fpsAfterOptimization;  // FPS after optimization
    volatile LONG fpsImprovementPct;     // FPS improvement percentage
    
    // Queue statistics
    volatile LONG inputQueueDepth;       // Current input queue depth
    volatile LONG outputQueueDepth;      // Current output queue depth
    volatile LONG maxInputQueueDepth;    // Peak input queue usage
    volatile LONG maxOutputQueueDepth;   // Peak output queue usage
};

// ================================================================
// Public API
// ================================================================

// Initialize the multithreaded nameplate renderer
bool Init();

// Shutdown and cleanup
void Shutdown();

// Called from main thread on each frame (for result processing and stats)
void OnFrame(DWORD mainThreadId);

// Get current statistics
Stats GetStats();

} // namespace NameplateMT

#endif // NAMEPLATE_BATCH_H
