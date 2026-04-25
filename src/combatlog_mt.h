#pragma once
// ================================================================
// Multithreaded Combat Log Parser for wow_optimize.dll — build 12340
// 
// WHAT: Offloads combat log event processing from main thread to
//       a dedicated worker thread using lock-free queue.
// WHY:  Combat log parsing consumes 40-60% of main thread CPU in
//       raids/PvP, causing FPS drops and UI lag.
// HOW:  1. Hook sub_750400 (combat log entry creation)
//       2. Copy event data to lock-free queue (4096 entries)
//       3. Worker thread dequeues and processes events asynchronously
//       4. Stats tracking: queued, processed, dropped, parse time
// ADDRESSES:
//   - sub_750400: 0x00750400 (combat log entry creation)
//   - ActiveListHead: 0x00ADB97C (linked list of entries)
// STATUS: Experimental — colossal-scale optimization
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
