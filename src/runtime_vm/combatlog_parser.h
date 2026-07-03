#pragma once
// ============================================================================
// Module: combatlog_parser.h
// Description: C-level combat log event pre-parser API
// ============================================================================

#include <cstdint>

// Parse a combat log event at C level before Lua string formatting.
// Returns true if event was classified and stored, false if unknown type.
bool CombatLogParser_ParseEvent(const char* eventType, uint64_t srcGUID,
                                 uint64_t dstGUID, uint32_t spellId,
                                 int32_t amount, uint32_t flags);

// Read next pre-parsed event from ring buffer. Returns false if empty.
// out must point to a 40-byte buffer (ParsedCombatEvent struct).
bool CombatLogParser_ReadNext(void* out);

// Get cumulative stats.
void CombatLogParser_GetStats(long* parsed, long* skipped);

// Install/initialize the parser.
bool InstallCombatLogParser();

// Shutdown and reset.
void ShutdownCombatLogParser();