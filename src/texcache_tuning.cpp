// Resident-texture cache budget tuning. See texcache_tuning.h for the summary.
//
// WoW's texture cache (RTTI CGxTexCache) tracks resident bytes in the low dword of
// qword_B49C98 (0x00B49C98) and trims down to a byte budget held in the HIGH dword
// (0x00B49C9C). The eviction routine sub_4B6AE0 frees LRU entries while
// current(LO) > budget(HI). The budget is produced by sub_4B6580:
//
//     v1 = 0x4000000;                       // 64 MB
//     if (system_mem(sub_86B4C0) <= 1 GB) v1 = 0x2000000;   // 32 MB
//     if (device_type == 2) v1 = 0;         // null/headless device
//     budget = clamp(requested_cvar, 0, v1);
//
// So on any modern machine the resident-texture budget is capped at 64 MB -- a
// 2008-era figure. HD-client texture sets are far larger, so 64 MB forces near-
// constant eviction + reload: that stutters AND churns the large-block heap into
// VA fragmentation (every evict frees a multi-MB block, every reload allocates a
// different-size one). Raising the budget keeps the working set resident.
//
// We do NOT touch the eviction logic -- only the threshold it trims to. WoW still
// caps growth at the (new) budget, so memory cannot run away.
//
// Single-client only: a larger resident set costs VA, and multi-client HD is
// already 32-bit VA-constrained, so the stock budget is preserved there.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include "texcache_tuning.h"

extern "C" void Log(const char* fmt, ...);
extern bool g_isMultiClient;

// HIGH dword of qword_B49C98 = resident-texture byte budget.
static int* const TEXCACHE_BUDGET = (int*)0x00B49C9C;

// WoW's modern default cap; we only act on a value at or below this so we never
// fight a deliberately-smaller budget or stomp the 0 (null-device) case.
static const int STOCK_MAX_BYTES = 0x04000000;   // 64 MB

// Target resident budget. 128 MB is a conservative 2x of stock -- a large cut in
// eviction churn with a bounded VA cost. Tunable: raise for more residency (needs
// /3GB headroom for heavy HD sets), lower if VA is tight.
static const int TARGET_BYTES = 0x08000000;      // 128 MB

static bool g_logged = false;

static void RaiseBudget() {
    __try {
        int cur = *TEXCACHE_BUDGET;
        // Act only once WoW has installed its sane default (>0 and <= stock cap).
        // After we raise it to TARGET (> STOCK_MAX) this test is false, so we stay
        // quiet until a device reset puts the stock 64 MB back -- self-throttling.
        if (cur > 0 && cur <= STOCK_MAX_BYTES) {
            *TEXCACHE_BUDGET = TARGET_BYTES;
            if (!g_logged) {
                Log("[TexCache] Resident-texture budget %dMB -> %dMB (single-client; eviction logic unchanged)",
                    cur >> 20, TARGET_BYTES >> 20);
                g_logged = true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void InitTexCacheTuning() {
    if (g_isMultiClient) {
        Log("[TexCache] Budget tuning OFF (multi-client: preserve stock budget for VA headroom)");
        return;
    }
    Log("[TexCache] Resident-texture budget tuning armed (target %dMB, re-asserted after device resets)",
        TARGET_BYTES >> 20);
    RaiseBudget();   // apply now if the cache is already initialized
}

void TexCacheTuning_Tick() {
    if (g_isMultiClient) return;
    RaiseBudget();   // cheap: usually reads one int and returns (see self-throttle note)
}
