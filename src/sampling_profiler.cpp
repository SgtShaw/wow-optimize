// ================================================================
// Sampling Profiler — lightweight in-DLL EIP sampler
// ================================================================
// Background thread samples the main thread's EIP every ~1ms using
// SuspendThread/GetThreadContext/ResumeThread. Each sample is bucketed
// by the nearest known function address. On shutdown, the top-50 hot
// functions are dumped to the log sorted by sample count.
//
// Known-address table is built from IDA-verified WoW.exe addresses
// (see CONTEXT.md). Addresses outside known ranges are bucketed as
// "unknown_wow" (in .text but not in our table) or "system" (outside
// WoW's image entirely — kernel, driver, or system DLL time).
// ================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include "sampling_profiler.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace SamplingProfiler {

// ---- configuration ------------------------------------------------
static constexpr DWORD SAMPLE_INTERVAL_MS = 1;      // target interval
static constexpr int   MAX_KNOWN_FUNCS    = 256;     // address table size
static constexpr int   TOP_N              = 50;      // functions to dump
static constexpr uintptr_t WOW_BASE       = 0x00400000;
static constexpr uintptr_t WOW_END        = 0x00BFFFFF;  // wow.exe image range

// ---- known-function table -----------------------------------------
// Each entry: { address, name }. Sorted by address for binary search.
// Populated at Init() from the verified address list.
struct FuncEntry {
    uintptr_t   addr;
    const char* name;
};

struct SampleBucket {
    uintptr_t   addr;       // nearest known function (or 0 for unknown)
    const char* name;       // null if unknown
    uint64_t    count;
};

static FuncEntry    g_knownFuncs[MAX_KNOWN_FUNCS];
static int          g_knownCount = 0;

// ---- sample storage -----------------------------------------------
// We store raw EIP values and aggregate at dump time. This avoids
// lock contention during sampling. The ring buffer is written only
// by the sampler thread and read only at shutdown (after the thread
// is joined), so no synchronization is needed beyond the atomic
// write index.
static constexpr int RING_SIZE = 1 << 20;  // ~1M samples (~17 min at 1ms)
static volatile uintptr_t g_ring[RING_SIZE];
static volatile uint64_t  g_writeIdx = 0;
static volatile uint64_t  g_totalSamples = 0;

// ---- state --------------------------------------------------------
static HANDLE  g_mainThread  = nullptr;
static HANDLE  g_samplerThread = nullptr;
static volatile bool g_running = false;
static HMODULE g_wowModule = nullptr;

// ---- populate known-function table --------------------------------
// Add entries here as new hooks/targets are verified in IDA.
// Keep sorted by address for binary-search lookup.
static void BuildKnownFuncTable() {
    // Format: { RVA_or_VA, "symbol_name" }
    // Using absolute VAs (base 0x400000).
    static const FuncEntry table[] = {
        // --- CRT / memory ---
        { 0x0040BB80, "memset" },
        { 0x0040CB10, "memcpy" },
        { 0x004112F8, "_msize" },
        { 0x00412FC7, "free" },
        { 0x00415074, "malloc" },
        { 0x00416A56, "calloc" },
        { 0x00416A95, "realloc" },
        { 0x00416CB0, "_recalloc" },

        // --- Math / transform library ---
        { 0x004C1B30, "CMatrix::TranslateLocal" },
        { 0x004C1F00, "CMatrix::Multiply" },
        { 0x004C2120, "CMatrix::ScalarMul" },
        { 0x004C21B0, "sub_4C21B0_pt_x_mat4" },
        { 0x004C2210, "RowAffinePoint" },
        { 0x004C2270, "sub_4C2270_vec4_x_mat4" },
        { 0x004C2300, "InPlacePointXform" },
        { 0x004C23D0, "CMatrix::Transpose" },
        { 0x004C2440, "CMatrix_AdjugateDet" },
        { 0x004C2FC0, "CMatrix::InvertRigid" },
        { 0x004C3420, "C3Vector::Normalize" },
        { 0x004C3600, "C3Vector::NormalizeGuarded" },

        // --- Object manager ---
        { 0x004D3790, "TLS_Accessor" },
        { 0x004D4DB0, "ClntObjMgrObjectPtr" },

        // --- Network / serialization ---
        { 0x00468D00, "NetPacketSend" },
        { 0x0047B340, "CDataStore_GetBytes" },
        { 0x0076DC20, "CDataStore_GetWowGUID" },

        // --- World / cleanup ---
        { 0x00528C30, "WorldExitCleanup" },
        { 0x005D9D90, "ObjMgrTeardownWalk" },

        // --- FrameScript / events ---
        { 0x0048E680, "FrameScript_Dispatch" },
        { 0x0081AC90, "FrameScript_SignalEvent" },

        // --- Lua VM core ---
        { 0x0084D9C0, "index2adr" },
        { 0x0084E030, "lua_tonumber" },
        { 0x0084E0B0, "lua_toboolean" },
        { 0x0084E1C0, "lua_touserdata" },
        { 0x0084E2A0, "lua_pushnumber" },
        { 0x0084E670, "lua_rawgeti" },
        { 0x0084E8D0, "lua_rawget" },
        { 0x0084EBF0, "lua_pcall" },
        { 0x0084F9F0, "luaL_checklstring" },

        // --- Lua internals ---
        { 0x00856C80, "luaS_newlstr" },
        { 0x00856E50, "luaV_tonumber" },
        { 0x00857900, "luaV_concat" },
        { 0x0085BC10, "luaV_gettable" },
        { 0x0085C430, "luaH_getstr" },
        { 0x0085C6F0, "LuaH_resize" },
        { 0x0085CAB0, "luaH_newkey" },

        // --- Rendering / culling ---
        { 0x00821A20, "M2_DrawBatchBuilder" },
        { 0x00960D20, "Lua_Model_SetLight" },
        { 0x00979110, "CQuaternion::Normalize" },
        { 0x00981D40, "ParticleSpawn_Init" },
        { 0x00983490, "RayTriIntersect16" },
        { 0x009836B0, "RayTriIntersect32" },
        { 0x009839E0, "CFrustum::IsAABBVisible" },
        { 0x00983D70, "CFrustum::IsPointVisible" },

        // --- CRT string/memory (static) ---
        { 0x0076E5A0, "free_wrapper" },
        { 0x0076E780, "_strnicmp" },
        { 0x0076ED20, "strncpy" },
        { 0x0076EE30, "strlen" },

        // --- Combat text ---
        { 0x00608880, "CombatText_EventInit" },

        // --- Misc ---
        { 0x0081B510, "EventNameWrapper" },
    };

    g_knownCount = 0;
    for (const auto& e : table) {
        if (g_knownCount >= MAX_KNOWN_FUNCS) break;
        g_knownFuncs[g_knownCount++] = e;
    }

    // Sort by address for binary search
    std::sort(g_knownFuncs, g_knownFuncs + g_knownCount,
              [](const FuncEntry& a, const FuncEntry& b) { return a.addr < b.addr; });
}

// Find the nearest known function at or below the given address.
// Returns nullptr if no known function is within 4KB (likely not in a
// function we care about, or in an unlisted helper).
static const FuncEntry* FindNearestFunc(uintptr_t eip) {
    if (g_knownCount == 0) return nullptr;

    // Binary search for the largest addr <= eip
    int lo = 0, hi = g_knownCount - 1;
    int best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_knownFuncs[mid].addr <= eip) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    if (best < 0) return nullptr;

    // Only match if within 4KB of the function start (reasonable
    // upper bound for a single WoW function body).
    uintptr_t delta = eip - g_knownFuncs[best].addr;
    if (delta > 4096) return nullptr;

    return &g_knownFuncs[best];
}

// ---- sampler thread -----------------------------------------------
static DWORD WINAPI SamplerThreadProc(LPVOID) {
    CONTEXT ctx;
    ctx.ContextFlags = CONTEXT_CONTROL;  // just EIP + segment regs

    while (g_running) {
        // Suspend → read → resume. The window is ~microseconds;
        // WoW won't notice. Same technique crash dumpers use.
        if (SuspendThread(g_mainThread) != (DWORD)-1) {
            if (GetThreadContext(g_mainThread, &ctx)) {
                uintptr_t eip = (uintptr_t)ctx.Eip;
                uint64_t idx = g_writeIdx % RING_SIZE;
                g_ring[idx] = eip;
                g_writeIdx++;
                g_totalSamples++;
            }
            ResumeThread(g_mainThread);
        }

        Sleep(SAMPLE_INTERVAL_MS);
    }

    return 0;
}

// ---- aggregation + dump -------------------------------------------
static void DumpResults() {
    uint64_t total = g_totalSamples;
    if (total == 0) {
        Log("[SamplingProfiler] No samples collected");
        return;
    }

    // Aggregate: count samples per nearest-known-function.
    // Unknown WoW addresses and system addresses get their own buckets.
    static constexpr int MAX_BUCKETS = MAX_KNOWN_FUNCS + 2;
    SampleBucket buckets[MAX_BUCKETS];
    int bucketCount = 0;

    // Initialize buckets from known funcs
    for (int i = 0; i < g_knownCount && bucketCount < MAX_BUCKETS; i++) {
        buckets[bucketCount].addr  = g_knownFuncs[i].addr;
        buckets[bucketCount].name  = g_knownFuncs[i].name;
        buckets[bucketCount].count = 0;
        bucketCount++;
    }

    // Two extra buckets for unknowns
    int unknownWowIdx = -1;
    int systemIdx = -1;
    if (bucketCount < MAX_BUCKETS) {
        unknownWowIdx = bucketCount;
        buckets[bucketCount].addr  = 0;
        buckets[bucketCount].name  = "unknown_wow";
        buckets[bucketCount].count = 0;
        bucketCount++;
    }
    if (bucketCount < MAX_BUCKETS) {
        systemIdx = bucketCount;
        buckets[bucketCount].addr  = 0;
        buckets[bucketCount].name  = "system_dll";
        buckets[bucketCount].count = 0;
        bucketCount++;
    }

    // Walk the ring buffer and bucket each sample
    uint64_t n = (total < RING_SIZE) ? total : RING_SIZE;
    uint64_t startIdx = (total <= RING_SIZE) ? 0 : (total - RING_SIZE);

    for (uint64_t i = 0; i < n; i++) {
        uintptr_t eip = g_ring[(startIdx + i) % RING_SIZE];

        const FuncEntry* f = FindNearestFunc(eip);
        if (f) {
            // Find the bucket for this function (linear scan — only at
            // dump time, not on the hot path).
            for (int b = 0; b < g_knownCount; b++) {
                if (buckets[b].addr == f->addr) {
                    buckets[b].count++;
                    goto next_sample;
                }
            }
        }

        // Not matched to a known function
        if (eip >= WOW_BASE && eip <= WOW_END) {
            if (unknownWowIdx >= 0) buckets[unknownWowIdx].count++;
        } else {
            if (systemIdx >= 0) buckets[systemIdx].count++;
        }
        next_sample:;
    }

    // Sort by count descending
    std::sort(buckets, buckets + bucketCount,
              [](const SampleBucket& a, const SampleBucket& b) {
                  return a.count > b.count;
              });

    // Dump top-N
    Log("[SamplingProfiler] === TOP %d HOT FUNCTIONS (%llu total samples) ===",
        TOP_N, (unsigned long long)total);

    int printed = 0;
    for (int i = 0; i < bucketCount && printed < TOP_N; i++) {
        if (buckets[i].count == 0) break;
        double pct = 100.0 * (double)buckets[i].count / (double)total;
        if (buckets[i].name) {
            Log("[SamplingProfiler] %3d. %-30s  %8llu samples (%5.2f%%)",
                printed + 1, buckets[i].name,
                (unsigned long long)buckets[i].count, pct);
        } else {
            Log("[SamplingProfiler] %3d. %-30s  %8llu samples (%5.2f%%)",
                printed + 1, "(null)",
                (unsigned long long)buckets[i].count, pct);
        }
        printed++;
    }

    Log("[SamplingProfiler] === END PROFILE ===");
}

// ---- public API ---------------------------------------------------
bool Init(HANDLE mainThread) {
    if (!mainThread) return false;

    g_mainThread = mainThread;
    g_writeIdx = 0;
    g_totalSamples = 0;
    memset((void*)g_ring, 0, sizeof(g_ring));

    BuildKnownFuncTable();

    g_running = true;
    g_samplerThread = CreateThread(
        nullptr,
        64 * 1024,           // small stack — we barely use any
        SamplerThreadProc,
        nullptr,
        0,                   // run immediately
        nullptr
    );

    if (!g_samplerThread) {
        Log("[SamplingProfiler] FAILED to create sampler thread (err=%u)", GetLastError());
        g_running = false;
        return false;
    }

    // Lowest priority — never compete with WoW's threads
    SetThreadPriority(g_samplerThread, THREAD_PRIORITY_IDLE);

    Log("[SamplingProfiler] INIT (interval=%dms, known_funcs=%d, ring=%d entries)",
        SAMPLE_INTERVAL_MS, g_knownCount, RING_SIZE);
    return true;
}

void Shutdown() {
    if (!g_running) return;

    g_running = false;

    // Wait for the sampler thread to exit (it checks g_running each iteration)
    if (g_samplerThread) {
        WaitForSingleObject(g_samplerThread, 2000);
        CloseHandle(g_samplerThread);
        g_samplerThread = nullptr;
    }

    DumpResults();

    Log("[SamplingProfiler] SHUTDOWN (total_samples=%llu)",
        (unsigned long long)g_totalSamples);
}

bool IsActive() { return g_running; }

uint64_t GetSampleCount() { return g_totalSamples; }

} // namespace SamplingProfiler