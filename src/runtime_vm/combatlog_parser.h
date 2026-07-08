#pragma once
// ============================================================================
// Module: combatlog_parser.h
// Description: C-level combat log event pre-parser and aggregator API
// ============================================================================

struct lua_State;

// Process and aggregate combat log event from the Lua stack
void CombatLogParser_ProcessEvent(void* L, int fieldCount);

// Periodically check and service Lua side requests (get stats / reset stats)
void CombatLogParser_Update(void* L);

// Install/initialize the parser
bool InstallCombatLogParser();

// Shutdown and reset
void ShutdownCombatLogParser();