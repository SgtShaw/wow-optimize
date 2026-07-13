// ============================================================================
// Module: cvar_watchdog.cpp
// Description: Protects game configuration variables (CVars) from null pointer exceptions and resets.
// Safety & Threading: Main thread only.
// ============================================================================

#include <windows.h>
#include <cstdint>
#include "version.h"

extern "C" void Log(const char* fmt, ...);

struct CvarWatchEntry {
    uintptr_t  addr;       // address of the CVar pointer global
    const char* name;      // CVar name for logging
    uint32_t   safeValue;  // safe default value (0 = just warn)
    bool       isPointer;  // true if the global is a pointer to a CVar struct
};

static CvarWatchEntry g_watch[] = {
    // ---- Render init globals (crash at 0x87307D) ----
    { 0x00D43024, "dword_D43024 (render init)",     0, false },
    { 0x00C5DF88, "dword_C5DF88 (render init)",     0, false },

    // ---- Sound CVars (crash at 0x4C5E7B, 0x4D1D53) ----
    { 0x00B4A3A4, "Sound_EnableSoundWhenGameIsInBG",      0, true  },
    { 0x00B4AEFC, "Sound_EnableReverb pointer",            0, true  },
    { 0x009F22CC, "Sound_EnableSoundWhenGameIsInBG (2)",   0, true  },
    { 0x009F23B0, "Sound_OutputDriverIndex",               0, true  },
    { 0x009F2488, "Sound_EnableAmbience",                  0, true  },
    { 0x009F24A0, "Sound_EnableMusic",                     0, true  },
    { 0x009F24C8, "Sound_EnableSFX",                       0, true  },
    { 0x009F24DC, "Sound_EnableAllSound",                  0, true  },
    { 0x009F2544, "Sound_ChaosMode",                       0, true  },
    { 0x009F2554, "Sound_EnableDSPEffects",                0, true  },
    { 0x009F256C, "Sound_MasterVolume",                    0, true  },
    { 0x009F2580, "Sound_OutputDriverName",                0, true  },
    { 0x009F2598, "Sound_NumChannels",                     0, true  },
    { 0x009F25AC, "Sound_EnableSoftwareHRTF",              0, true  },
    { 0x009F25C8, "Sound_EnableReverb",                    0, true  },
    { 0x009F25F0, "Sound_ZoneMusicNoDelay",                0, true  },
    { 0x009F2820, "Sound_ListenerAtCharacter",             0, true  },

    // ---- Visual alert (crash at 0x5E90D0) ----
    { 0x00C24238, "dword_C24238 (visual alert)",     0, false },

    // ---- Taint system (secure execution) ----
    { 0x00D4139C, "Taint cell (secure execution)",    0, false },
    { 0x00D413A0, "Taint check flag",                 0, false },

    // ---- Lua state ----
    { 0x00D3F78C, "lua_State* (global Lua state)",    0, true  },
    { 0x00D3F790, "lua_State stack pointer (L+4)",    0, true  },

    // ---- CRT heap ----
    { 0x00B31684, "_crtheap (CRT heap handle)",       0, false },
};

static bool g_initialized = false;

void CvarWatchdog_Check()
{
    if (g_initialized) return;
    g_initialized = true;

    Log("[CvarWatchdog] Scanning %d critical CVars...",
        (int)(sizeof(g_watch)/sizeof(g_watch[0])));

    int fixed = 0;
    bool anyCorrupt = false;
    for (size_t i = 0; i < sizeof(g_watch)/sizeof(g_watch[0]); i++) {
        CvarWatchEntry& e = g_watch[i];
        uintptr_t val = *(uintptr_t*)e.addr;

        if (e.isPointer) {
            if (val < 0x10000) {
                anyCorrupt = true;
                Log("[CvarWatchdog] CORRUPT: %s is NULL (0x%08X) — "
                    "dependent code may crash", e.name, e.addr);
                if (e.safeValue != 0) {
                    *(uintptr_t*)e.addr = e.safeValue;
                    fixed++;
                }
            }
        } else {
            if (val == 0 || val == 0xFFFFFFFF || val == 0xCDCDCDCD) {
                anyCorrupt = true;
                Log("[CvarWatchdog] CORRUPT: %s = 0x%08X at 0x%08X",
                    e.name, (unsigned)val, (unsigned)e.addr);
                if (e.safeValue != 0) {
                    *(uint32_t*)e.addr = e.safeValue;
                    fixed++;
                }
            }
        }
    }

    if (fixed > 0) {
        Log("[CvarWatchdog] Fixed %d corrupted CVars", fixed);
    } else if (anyCorrupt) {
        Log("[CvarWatchdog] Scan completed: corrupted CVars found (reported above).");
    } else {
        Log("[CvarWatchdog] All CVars OK");
    }
}

// Called from dllmain.cpp near startup, after the 5s delay
bool InitCvarWatchdog()
{
    CvarWatchdog_Check();
    return true;
}
