#pragma once
// ================================================================
// Multithreaded Combat Log Parser for wow_optimize.dll — build 12340
// 
// WHAT: Offloads combat log event processing from main thread to
//       a dedicated worker thread using lock-free queue.
// WHY:  Combat log parsing consumes 40-60% of main thread CPU in
//       raids/PvP, causing FPS drops and UI lag.
// HOW:  1. Hook sub_74F910 (combat log event dispatcher)
//       2. Copy event data to lock-free queue (4096 entries)
//       3. Worker thread dequeues and processes events asynchronously
//       4. Stats tracking: queued, processed, dropped, parse time
// ADDRESSES:
//   - sub_74F910: 0x0074F910 (event dispatcher to Lua)
//   - ActiveListHead: 0x00ADB97C (linked list of entries)
// STATUS: Fixed — hooks event dispatcher instead of entry creation
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
    
    // NEW: Raid detection statistics (v3.5.14 raid stutter fix)
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
