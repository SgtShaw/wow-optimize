#pragma once
// ================================================================
// Multithreaded Combat Log Parser
// 
// ================================================================

#ifndef COMBATLOG_MT_H
#define COMBATLOG_MT_H

#include <windows.h>
#include <cstdint>

namespace CombatLogMT {

// Statistics structure
struct Stats {
    volatile LONG eventsQueued;      // Total events queued by main thread
    volatile LONG eventsProcessed;   // Total events processed by worker thread
    volatile LONG eventsDropped;     // Events dropped due to queue overflow
    volatile LONG eventsInvalid;     // Invalid events skipped
    volatile LONG queueDepth;        // Current queue depth
    double totalParseTimeMs;         // Total parse time in milliseconds
    
    // NEW: Performance monitoring for raid stutter fix
    volatile LONG totalDispatches;           // Total dispatch calls
    volatile LONG entriesScannedThisFrame;  // Entries processed in last frame
    volatile LONG entriesDroppedDueToBudget; // Entries skipped due to time/count budget
    uint32_t maxEntriesPerFrame;            // Current frame budget (entries)
    uint32_t maxScanTimeUs;                 // Current time budget (microseconds)
    
    // Raid detection statistics
    volatile LONG raidDisableCount;         // Times COMBATLOG_MT disabled in raids
    volatile LONG openWorldEnableCount;     // Times COMBATLOG_MT enabled in open world
    volatile LONG instanceType;             // Current instance type (0=none, 1=party, 2=raid, 3=pvp, 4=arena)
};

// Initialize the multithreaded combat log parser
bool Init();

// Shutdown and cleanup
void Shutdown();

// Called from main thread on each frame (for stats updates)
void OnFrame(DWORD mainThreadId);

// Get current statistics
Stats GetStats();

} // namespace CombatLogMT

#endif // COMBATLOG_MT_H
