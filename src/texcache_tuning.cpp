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

// Target resident budget, chosen at init from available 32-bit VA. A larger
// resident set keeps an area's textures from being evicted/reloaded, which cuts
// both stutter and the large-block-heap fragmentation that churn causes -- but it
// holds that much VA. With /3GB (bcdedit /set increaseuserva 3072) there is room
// for 256MB; on a stock 2GB user-VA layout we keep it to 192MB.
// Lowered from 256/192 MB: on VA-tight HD clients the larger resident set
// enlarged the working set that Windows trims on alt-tab, so faulting it back
// stalled the main thread. A smaller budget trades a little eviction churn for
// much less paging pressure.
static const int TARGET_3GB_BYTES = 0x08000000;  // 128 MB (increaseuserva active)
static const int TARGET_2GB_BYTES = 0x06000000;  //  96 MB (stock 2GB user VA)
static int g_targetBytes = TARGET_2GB_BYTES;

static bool g_logged = false;

// True when the OS hands this 32-bit process >2GB of user address space, i.e.
// /3GB / increaseuserva is in effect. lpMaximumApplicationAddress is ~0x7FFEFFFF
// at 2GB and ~0xBFFEFFFF at 3GB.
static bool Has3GBUserVA() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (uintptr_t)si.lpMaximumApplicationAddress > 0x80000000;
}

static void RaiseBudget() {
    __try {
        int cur = *TEXCACHE_BUDGET;
        // Act only once WoW has installed its sane default (>0 and <= stock cap).
        // After we raise it to TARGET (> STOCK_MAX) this test is false, so we stay
        // quiet until a device reset puts the stock 64 MB back -- self-throttling.
        if (cur > 0 && cur <= STOCK_MAX_BYTES) {
            *TEXCACHE_BUDGET = g_targetBytes;
            if (!g_logged) {
                Log("[TexCache] Resident-texture budget %dMB -> %dMB (single-client; eviction logic unchanged)",
                    cur >> 20, g_targetBytes >> 20);
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
    bool va3 = Has3GBUserVA();
    g_targetBytes = va3 ? TARGET_3GB_BYTES : TARGET_2GB_BYTES;
    Log("[TexCache] Resident-texture budget tuning armed (target %dMB, %s, re-asserted after device resets)",
        g_targetBytes >> 20, va3 ? "/3GB user VA detected" : "stock 2GB user VA");
    RaiseBudget();   // apply now if the cache is already initialized
}

void TexCacheTuning_Tick() {
    if (g_isMultiClient) return;
    RaiseBudget();   // cheap: usually reads one int and returns (see self-throttle note)
}
