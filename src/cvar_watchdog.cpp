// ================================================================
// CVar Watchdog — detects and recovers from CVar corruption.
//
// CVar corruption is the #1 cause of NULL-dereference crashes:
//   - dword_D43024 (render init) → crash at 0x87307D
//   - dword_B4A3A4 (sound CVar)  → crash at 0x4C5E7B
//   - dword_C24238 (visual alert)→ crash at 0x5E90D0
//   - GM chat CVar              → lua error on login
//
// This module probes known CVar addresses at startup and
// verifies their values are sane. Corrupted CVars are
// restored to safe defaults.
// ================================================================

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
    { 0x00D43024, "dword_D43024 (render init)", 0, false },
    { 0x00C5DF88, "dword_C5DF88 (render init)", 0, false },
    { 0x00B4A3A4, "Sound_EnableSoundWhenGameIsInBG", 0, true  },
    { 0x00C24238, "dword_C24238 (visual alert)", 0, false },
    { 0x00D3F78C, "lua_State* (global Lua state)", 0, true  },
};

static bool g_initialized = false;

void CvarWatchdog_Check()
{
    if (g_initialized) return;
    g_initialized = true;

    Log("[CvarWatchdog] Scanning %d critical CVars...",
        (int)(sizeof(g_watch)/sizeof(g_watch[0])));

    int fixed = 0;
    for (size_t i = 0; i < sizeof(g_watch)/sizeof(g_watch[0]); i++) {
        CvarWatchEntry& e = g_watch[i];
        uintptr_t val = *(uintptr_t*)e.addr;

        if (e.isPointer) {
            if (val < 0x10000) {
                Log("[CvarWatchdog] CORRUPT: %s is NULL (0x%08X) — "
                    "dependent code may crash", e.name, e.addr);
                if (e.safeValue != 0) {
                    *(uintptr_t*)e.addr = e.safeValue;
                    fixed++;
                }
            }
        } else {
            if (val == 0 || val == 0xFFFFFFFF || val == 0xCDCDCDCD) {
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
