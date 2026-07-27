// ============================================================================
// Module: quality_governor.cpp
// Description: Lowers shadow quality when frames are consistently slow, and
//              restores the player's own value when they are not.
// Safety & Threading: Main thread only.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "quality_governor.h"
#include "version.h"
#include "core/config.h"
#include "diagnostics/frame_bench.h"
#include "diagnostics/crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

namespace QualityGovernor {

// The engine's CVar setter. Called directly rather than through a trampoline:
// this module does not hook it, it only observes writes reported to NoteCVarWrite.
typedef char (__fastcall* CVar_Set_fn)(void* cvar, void* edx, const char* value,
                                       char a3, char a4, char a5, char a6);
static const CVar_Set_fn g_cvarSet = (CVar_Set_fn)0x007668C0;

// ---------------------------------------------------------------------------
// What this manages
//
// Shadows only, deliberately. Draw distance and particle density already have
// their own scalers; adding a second opinion on the same setting produces two
// controllers fighting over one value, which is a worse failure than either alone.
// Shadows are the setting that has no governor, having lost the previous one for
// being unable to read what the player had chosen.
// ---------------------------------------------------------------------------
static const char* const CVAR_NAME = "extShadowQuality";

// A step down is worth taking only if it buys something. One level at a time,
// never below 1: level 0 turns shadows off entirely and looks like a bug to the
// player rather than an adaptation.
static const int MIN_LEVEL = 1;

// ---------------------------------------------------------------------------
// Thresholds
//
// Both are on the 95th percentile of the last few seconds, not on an
// instantaneous frame rate. The scaler this replaces smoothed 1000/elapsed with
// an EMA and switched on thresholds a single frame could cross, so walking
// through a zone moved shadow quality several times a minute. What a player
// notices is the tail.
//
// The two thresholds do not touch, and the dwell periods are asymmetric: quick to
// help, slow to undo. Together that makes oscillation impossible - recovering
// requires 30 seconds of frames comfortably better than the level that triggered
// a reduction.
// ---------------------------------------------------------------------------
static const double DEGRADE_P95_MS = 33.0;   // sustained worse than ~30fps at p95
static const double RESTORE_P95_MS = 20.0;   // sustained better than ~50fps at p95
static const DWORD  DEGRADE_DWELL_MS = 5000;
static const DWORD  RESTORE_DWELL_MS = 30000;
static const DWORD  MIN_ACTION_GAP_MS = 10000;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static void*  g_cvar        = nullptr;  // the CVar object, learned from a write
static int    g_userLevel   = -1;       // the player's own value. -1 = not seen yet
static int    g_current     = -1;       // what we last set, or the user's value
static bool   g_writingOurs = false;    // guards against reading back our own write

static DWORD  g_badSince    = 0;
static DWORD  g_goodSince   = 0;
static DWORD  g_lastAction  = 0;
static int    g_featureToken = -1;
static int    g_reductions  = 0;

static bool   g_enabled     = false;

void NoteCVarWrite(const char* name, const char* value) {
    if (!g_enabled || !name || !value) return;
    if (_stricmp(name, CVAR_NAME) != 0) return;

    // Our own write coming back around. Recording it would turn a reduction into
    // the new ceiling, and the player's setting would ratchet down and never
    // return - which is exactly the failure that made the previous version
    // destroy people's graphics settings.
    if (g_writingOurs) return;

    int v = atoi(value);
    if (v < 0 || v > 16) return;

    g_userLevel = v;
    g_current   = v;
    Log("[QualityGovernor] Player's shadow quality is %d - that is the ceiling", v);
}

// The CVar object is not something this module can look up; it arrives with the
// first write the client makes. Until then there is nothing to act on.
void NoteCVarObject(void* cvar, const char* name) {
    if (!g_enabled || !cvar || !name) return;
    if (_stricmp(name, CVAR_NAME) != 0) return;
    g_cvar = cvar;
}

static void Apply(int level, const char* why) {
    if (!g_cvar || level == g_current) return;

    char buf[16];
    _snprintf(buf, sizeof(buf), "%d", level);
    buf[sizeof(buf) - 1] = '\0';

    g_writingOurs = true;
    __try {
        g_cvarSet(g_cvar, nullptr, buf, 1, 0, 0, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_writingOurs = false;
        return;
    }
    g_writingOurs = false;

    Log("[QualityGovernor] Shadow quality %d -> %d (%s, p95 %.1f ms, player's setting is %d)",
        g_current, level, why, FrameBench::RecentP95Ms(), g_userLevel);
    CrashDumper::Trace("QualityGovernor: shadows %d -> %d (%s)", g_current, level, why);

    g_current = level;
    g_lastAction = GetTickCount();
    CrashDumper::FeatureHit(g_featureToken);
}

void OnFrame() {
    if (!g_enabled || g_userLevel < 0 || !g_cvar) return;

    double p95 = FrameBench::RecentP95Ms();
    if (p95 <= 0.0) return;                 // not enough frames yet

    DWORD now = GetTickCount();

    if (p95 >= DEGRADE_P95_MS) {
        g_goodSince = 0;
        if (g_badSince == 0) g_badSince = now;
    } else if (p95 <= RESTORE_P95_MS) {
        g_badSince = 0;
        if (g_goodSince == 0) g_goodSince = now;
    } else {
        // Between the two thresholds: neither condition holds, so neither timer
        // runs. This band is what stops a value hovering near a threshold from
        // being treated as a sustained trend.
        g_badSince = 0;
        g_goodSince = 0;
        return;
    }

    if (g_lastAction != 0 && (now - g_lastAction) < MIN_ACTION_GAP_MS) return;

    if (g_badSince != 0 && (now - g_badSince) >= DEGRADE_DWELL_MS) {
        if (g_current > MIN_LEVEL) {
            Apply(g_current - 1, "frames sustained slow");
            g_reductions++;
        }
        g_badSince = 0;
    } else if (g_goodSince != 0 && (now - g_goodSince) >= RESTORE_DWELL_MS) {
        if (g_current < g_userLevel) {
            Apply(g_current + 1, "frames recovered");
        }
        g_goodSince = 0;
    }
}

bool Init() {
    g_enabled = Config::g_settings.OptQualityGovernor;
    if (!g_enabled) {
        Log("[QualityGovernor] DISABLED via configuration");
        return false;
    }
    g_featureToken = CrashDumper::FeatureTokenForCounting("QualityGovernor");
    Log("[QualityGovernor] Active - shadow quality follows the frame-time tail, "
        "never above the player's own setting (degrade p95>%.0fms/%us, restore "
        "p95<%.0fms/%us)",
        DEGRADE_P95_MS, DEGRADE_DWELL_MS / 1000, RESTORE_P95_MS, RESTORE_DWELL_MS / 1000);
    return true;
}

void Shutdown() {
    if (!g_enabled) return;

    // Put the player's setting back. The previous version could not do this - it
    // never knew what to restore - and left people with shadows it had lowered.
    if (g_userLevel >= 0 && g_current != g_userLevel) {
        Apply(g_userLevel, "restoring the player's setting on shutdown");
    }
    if (g_reductions > 0) {
        Log("[QualityGovernor] Reduced shadow quality %d time(s) this session", g_reductions);
    } else {
        Log("[QualityGovernor] Never had to reduce shadow quality this session");
    }
}

} // namespace QualityGovernor
