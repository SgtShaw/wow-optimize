#include "perf_diagnostics.h"
#include "version.h"
#include "crash_dumper.h"
#include <psapi.h>
#include <cstdio>
#include <atomic>

extern "C" void Log(const char* fmt, ...);
extern void CrashDumper_DumpHookTrace(int count);

namespace PerfDiagnostics {

static DWORD g_lastDiagTick = 0;
static std::atomic<long> g_stutterCount{0};

static float* const g_playerX = (float*)0x00BE1F30;
static float* const g_playerY = (float*)0x00BE1F34;

void LogPerformanceSnapshot(double elapsedMs) {
    DWORD now = GetTickCount();
    if (now - g_lastDiagTick < 5000) return; // Rate-limit to once every 5 seconds
    g_lastDiagTick = now;
    
    g_stutterCount.fetch_add(1, std::memory_order_relaxed);
    
    Log("[PerfDiag] === STUTTER DETECTED (Frame duration: %.1f ms) ===", elapsedMs);
    
    // 1. Coordinates & Zone
    if (g_playerX && g_playerY) {
        Log("[PerfDiag]   Player position: X=%.2f, Y=%.2f", *g_playerX, *g_playerY);
    }
    
    // 2. Memory State
    PROCESS_MEMORY_COUNTERS pmc = {};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        Log("[PerfDiag]   Working Set: %.1f MB  Private Bytes: %.1f MB", 
            pmc.WorkingSetSize / (1024.0 * 1024.0), 
            pmc.PagefileUsage / (1024.0 * 1024.0));
    }
    
    // 3. Virtual Address Space Check
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = 0x10000;
    SIZE_T largestFree = 0, totalFree = 0;
    while (addr < 0x7FFF0000) {
        if (VirtualQuery((void*)addr, &mbi, sizeof(mbi))) {
            if (mbi.State == MEM_FREE) {
                if (mbi.RegionSize > largestFree) largestFree = mbi.RegionSize;
                totalFree += mbi.RegionSize;
            }
            addr += mbi.RegionSize;
            if (mbi.RegionSize == 0) addr += 0x10000;
        } else {
            addr += 0x10000;
        }
    }
    Log("[PerfDiag]   VA Total Free: %.1f MB  Largest Block: %.1f MB%s",
        totalFree / (1024.0 * 1024.0),
        largestFree / (1024.0 * 1024.0),
        (largestFree < 64 * 1024 * 1024) ? " [WARNING: FRAGMENTED]" : "");
        
    // 4. Feature states and usage
    Log("[PerfDiag]   Optimization features status:");
    FeatureState features[64];
    int fcount = CrashDumper::GetFeatureStates(features, 64);
    for (int i = 0; i < fcount; i++) {
        if (features[i].active) {
            Log("[PerfDiag]     %-28s calls=%lld errors=%lld", 
                features[i].name ? features[i].name : "(null)", 
                features[i].callCount, 
                features[i].errorCount);
        }
    }
    
    // 5. Dump last hook trace to pinpoint exactly what ran during this lag spike
    Log("[PerfDiag]   Last 16 hook calls before stutter:");
    CrashDumper_DumpHookTrace(16);
    
    Log("[PerfDiag] ==================================================");
}

void OnFrame(double elapsedMs) {
    #if !TEST_DISABLE_SAMPLING_PROFILER
    // If a frame takes longer than 100ms (10 FPS or below), it's a severe stutter
    if (elapsedMs > 100.0) {
        LogPerformanceSnapshot(elapsedMs);
    }
    #endif
}

bool Init() {
    g_lastDiagTick = 0;
    g_stutterCount.store(0);
    Log("[PerfDiag] Performance Diagnostic Monitor Active (100ms stutter trigger)");
    return true;
}

void Shutdown() {
}

} // namespace PerfDiagnostics
