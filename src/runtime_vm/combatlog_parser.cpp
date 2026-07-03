// ============================================================================
// Module: combatlog_parser.cpp
// Description: C-level combat log event parser that pre-parses events before
//              Lua string formatting, reducing GC pressure from temporary
//              string allocations during raids.
// Safety & Threading: Main thread only. Integrates with existing CombatLogOpt.
// ============================================================================

#include <windows.h>
#include <cstdint>
#include <cstring>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

#if !TEST_DISABLE_COMBATLOG_PARSER

// Pre-parsed combat log event types (subset of WoW's COMBAT_LOG_EVENT types)
enum CombatEventType : uint32_t {
    CLE_SWING_DAMAGE       = 0x01,
    CLE_SPELL_DAMAGE       = 0x02,
    CLE_SPELL_HEAL         = 0x03,
    CLE_SPELL_AURA_APPLIED = 0x04,
    CLE_SPELL_AURA_REMOVED = 0x05,
    CLE_UNIT_DIED          = 0x06,
    CLE_UNKNOWN            = 0xFF
};

// Pre-parsed event structure (avoids repeated string parsing in Lua)
struct ParsedCombatEvent {
    CombatEventType type;
    uint64_t        sourceGUID;
    uint64_t        destGUID;
    uint32_t        spellId;
    int32_t         amount;
    uint32_t        flags;
    bool            valid;
};

// Ring buffer for pre-parsed events
static constexpr int CLE_BUFFER_SIZE = 1024;
static constexpr int CLE_BUFFER_MASK = CLE_BUFFER_SIZE - 1;

static ParsedCombatEvent g_cleBuffer[CLE_BUFFER_SIZE] = {};
static volatile LONG g_cleWritePos = 0;
static volatile LONG g_cleReadPos = 0;
static volatile LONG g_cleParsed = 0;
static volatile LONG g_cleSkipped = 0;

// Classify event type from WoW's event string prefix
static CombatEventType ClassifyEventType(const char* eventType) {
    if (!eventType) return CLE_UNKNOWN;

    // Fast prefix matching (WoW event names are well-known)
    if (strncmp(eventType, "SWING_DAMAGE", 12) == 0) return CLE_SWING_DAMAGE;
    if (strncmp(eventType, "SPELL_DAMAGE", 12) == 0) return CLE_SPELL_DAMAGE;
    if (strncmp(eventType, "SPELL_HEAL", 10) == 0)   return CLE_SPELL_HEAL;
    if (strncmp(eventType, "SPELL_AURA_APPLIED", 18) == 0) return CLE_SPELL_AURA_APPLIED;
    if (strncmp(eventType, "SPELL_AURA_REMOVED", 18) == 0) return CLE_SPELL_AURA_REMOVED;
    if (strncmp(eventType, "UNIT_DIED", 9) == 0)      return CLE_UNIT_DIED;

    return CLE_UNKNOWN;
}

// Parse a combat log event at C level before Lua sees it
bool CombatLogParser_ParseEvent(const char* eventType, uint64_t srcGUID,
                                 uint64_t dstGUID, uint32_t spellId,
                                 int32_t amount, uint32_t flags) {
    CombatEventType type = ClassifyEventType(eventType);
    if (type == CLE_UNKNOWN) {
        InterlockedIncrement(&g_cleSkipped);
        return false;
    }

    LONG pos = InterlockedIncrement(&g_cleWritePos) - 1;
    int idx = pos & CLE_BUFFER_MASK;

    g_cleBuffer[idx].type      = type;
    g_cleBuffer[idx].sourceGUID = srcGUID;
    g_cleBuffer[idx].destGUID   = dstGUID;
    g_cleBuffer[idx].spellId    = spellId;
    g_cleBuffer[idx].amount     = amount;
    g_cleBuffer[idx].flags      = flags;
    g_cleBuffer[idx].valid      = true;

    InterlockedIncrement(&g_cleParsed);
    return true;
}

// Read next parsed event (returns false if buffer empty)
bool CombatLogParser_ReadNext(ParsedCombatEvent* out) {
    if (!out) return false;

    LONG readPos = g_cleReadPos;
    if (readPos >= g_cleWritePos) return false; // Empty

    int idx = readPos & CLE_BUFFER_MASK;
    if (!g_cleBuffer[idx].valid) return false;

    *out = g_cleBuffer[idx];
    g_cleBuffer[idx].valid = false;
    InterlockedIncrement(&g_cleReadPos);
    return true;
}

// Get stats
void CombatLogParser_GetStats(LONG* parsed, LONG* skipped) {
    if (parsed)  *parsed  = g_cleParsed;
    if (skipped) *skipped = g_cleSkipped;
}

bool InstallCombatLogParser() {
    memset(g_cleBuffer, 0, sizeof(g_cleBuffer));
    g_cleWritePos = 0;
    g_cleReadPos = 0;
    g_cleParsed = 0;
    g_cleSkipped = 0;

    Log("[CombatLogParser] ACTIVE (%d-slot ring buffer, C-level pre-parse)", CLE_BUFFER_SIZE);
    CrashDumper::RegisterFeature("CombatLogParser");
    CrashDumper::FeatureSetActive("CombatLogParser", true);
    return true;
}

void ShutdownCombatLogParser() {
    LONG parsed, skipped;
    CombatLogParser_GetStats(&parsed, &skipped);
    Log("[CombatLogParser] Shutdown: %ld parsed, %ld skipped", parsed, skipped);
    memset(g_cleBuffer, 0, sizeof(g_cleBuffer));
}

#else // TEST_DISABLE_COMBATLOG_PARSER

bool CombatLogParser_ParseEvent(const char*, uint64_t, uint64_t, uint32_t, int32_t, uint32_t) { return false; }
bool CombatLogParser_ReadNext(void*) { return false; }
void CombatLogParser_GetStats(LONG* p, LONG* s) { if (p) *p = 0; if (s) *s = 0; }
bool InstallCombatLogParser() {
    Log("[CombatLogParser] DISABLED (test toggle)");
    CrashDumper::RegisterFeature("CombatLogParser");
    CrashDumper::FeatureSetActive("CombatLogParser", false);
    return false;
}
void ShutdownCombatLogParser() {}

#endif