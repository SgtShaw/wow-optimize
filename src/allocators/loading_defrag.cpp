// ============================================================================
// Module: loading_defrag.cpp
// Description: Speculative pre-committing and VA defragmentation during loading screens.
// Safety & Threading: Safe background thread execution. Main thread updates zone history.
// ============================================================================

#include "loading_defrag.h"
#include <windows.h>
#include <psapi.h>
#include <vector>
#include <string>
#include <atomic>

#pragma comment(lib, "psapi.lib")

// External functions and variables from other modules
extern "C" void Log(const char* fmt, ...);
extern "C" void mi_collect(bool force);
extern "C" void* mi_malloc(size_t size);
extern "C" void mi_free(void* p);

namespace LuaOpt {
    bool IsLoadingMode();
}

namespace LoadingDefrag {

// Function pointer definitions for Lua/FrameScript
typedef void (__cdecl *fn_lua_getfield)(uintptr_t L, int index, const char* k);
static const fn_lua_getfield lua_getfield_ = (fn_lua_getfield)0x0084E590;

typedef void (__cdecl *fn_FrameScript_Execute)(const char* code, const char* source, int unknown);
static const fn_FrameScript_Execute FrameScript_Execute_ = (fn_FrameScript_Execute)0x00819210;

#define LUA_GLOBALSINDEX (-10002)
#define LUA_TSTRING 4

// Background thread state
static HANDLE g_defragThread = nullptr;
static HANDLE g_defragEvent = nullptr;
static std::atomic<bool> g_shutdown{false};
static std::atomic<bool> g_loadingActive{false};

// Zone history tracking
static char g_currentZone[128] = "Unknown";
static char g_lastZone[128] = "Unknown";
static CRITICAL_SECTION g_zoneLock;

// Keep track of visited zones to speculatively adjust pre-commit amount
struct VisitedZone {
    std::string name;
    int visitCount;
};
static std::vector<VisitedZone> g_zoneHistory;

// Helper function to allocate and free blocks using Structured Exception Handling (SEH)
// without utilizing any C++ stack-allocated objects with destructors to avoid compiler C2712.
static void PrecommitAllocAndFree(size_t totalPrecommitMB) {
    void** smallPtrs = (void**)mi_malloc(32768 * sizeof(void*));
    void** mediumPtrs = (void**)mi_malloc(4096 * sizeof(void*));
    void** largePtrs = (void**)mi_malloc(32 * sizeof(void*));

    if (!smallPtrs || !mediumPtrs || !largePtrs) {
        if (smallPtrs) mi_free(smallPtrs);
        if (mediumPtrs) mi_free(mediumPtrs);
        if (largePtrs) mi_free(largePtrs);
        return;
    }

    memset(smallPtrs, 0, 32768 * sizeof(void*));
    memset(mediumPtrs, 0, 4096 * sizeof(void*));
    memset(largePtrs, 0, 32 * sizeof(void*));

    int smallCount = 0;
    int mediumCount = 0;
    int largeCount = 0;

    __try {
        // 1. Small blocks (UI, entities)
        for (int i = 0; i < 32768; i++) {
            void* p = mi_malloc(256);
            if (p) {
                *(volatile char*)p = 0xAA; // Fault in the page
                smallPtrs[i] = p;
                smallCount++;
            }
        }
        
        // 2. Medium blocks (Models)
        for (int i = 0; i < 4096; i++) {
            void* p = mi_malloc(4096);
            if (p) {
                volatile char* cp = (volatile char*)p;
                cp[0] = 0xBB;
                cp[4095] = 0xBB;
                mediumPtrs[i] = p;
                mediumCount++;
            }
        }

        // 3. Large blocks (Textures, chunks)
        size_t largeBlocksCount = (totalPrecommitMB == 128) ? 32 : 16;
        for (size_t i = 0; i < largeBlocksCount; i++) {
            size_t sz = 1024 * 1024;
            void* p = mi_malloc(sz);
            if (p) {
                volatile char* cp = (volatile char*)p;
                for (size_t offset = 0; offset < sz; offset += 4096) {
                    cp[offset] = 0xCC; // Touch every physical page
                }
                largePtrs[i] = p;
                largeCount++;
            }
        }
        
        Log("[LoadingDefrag] Speculatively allocated and faulted %d slabs", smallCount + mediumCount + largeCount);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("[LoadingDefrag] Exception occurred during pre-commit; safely reclaiming...");
    }

    // Safely free all allocated pointers
    for (int i = 0; i < 32768; i++) {
        if (smallPtrs[i]) mi_free(smallPtrs[i]);
    }
    for (int i = 0; i < 4096; i++) {
        if (mediumPtrs[i]) mi_free(mediumPtrs[i]);
    }
    for (int i = 0; i < 32; i++) {
        if (largePtrs[i]) mi_free(largePtrs[i]);
    }

    mi_free(smallPtrs);
    mi_free(mediumPtrs);
    mi_free(largePtrs);
}

// Speculatively pre-commit pages in mimalloc thread-local slabs
static void PerformSpeculativePrecommit() {
    Log("[LoadingDefrag] Starting speculative memory pre-commit...");
    
    // Determine allocation intensity based on zone history if needed
    size_t totalPrecommitMB = 64; // Default: pre-commit 64MB of slabs
    
    EnterCriticalSection(&g_zoneLock);
    bool knownZone = false;
    for (const auto& zone : g_zoneHistory) {
        if (zone.name == g_currentZone) {
            knownZone = true;
            if (zone.visitCount > 3) {
                // Heavily visited zones (e.g. Dalaran, main cities) get larger pre-commits
                totalPrecommitMB = 128;
            }
            break;
        }
    }
    LeaveCriticalSection(&g_zoneLock);
    
    Log("[LoadingDefrag] Target zone: '%s' (Pre-commit target: %dMB)", g_currentZone, (int)totalPrecommitMB);

    PrecommitAllocAndFree(totalPrecommitMB);
    Log("[LoadingDefrag] Speculative pre-commit complete. Caches pre-warmed.");
}

// Background thread function
static DWORD WINAPI DefragWorkerThread(LPVOID) {
    Log("[LoadingDefrag] Defrag background worker thread started");
    
    while (!g_shutdown.load(std::memory_order_relaxed)) {
        // Wait for a loading screen trigger
        WaitForSingleObject(g_defragEvent, INFINITE);
        
        if (g_shutdown.load(std::memory_order_relaxed)) break;
        
        if (g_loadingActive.load(std::memory_order_acquire)) {
            // Step 1: Pre-warm mimalloc caches for the new zone
            PerformSpeculativePrecommit();
            
            // Step 2: Aggressively defragment memory while loading is active
            Log("[LoadingDefrag] Defragmenter loop active (loading screen phase)");
            int compactionCount = 0;
            
            while (g_loadingActive.load(std::memory_order_relaxed) && 
                   !g_shutdown.load(std::memory_order_relaxed)) 
            {
                Sleep(200); // Check every 200ms
                
                // Trigger page release and compaction
                mi_collect(true);
                
                HANDLE heaps[64];
                DWORD heapCount = GetProcessHeaps(64, heaps);
                for (DWORD i = 0; i < heapCount && i < 16; i++) {
                    HeapCompact(heaps[i], 0);
                }
                
                compactionCount++;
            }
            
            // Step 3: Final sweep when loading screen finishes
            mi_collect(true);
            Log("[LoadingDefrag] Defragmenter loop finished (compactions run: %d)", compactionCount);
            
            ResetEvent(g_defragEvent);
        }
    }
    
    Log("[LoadingDefrag] Defrag background worker thread shutting down");
    return 0;
}

bool Init() {
    InitializeCriticalSection(&g_zoneLock);
    g_shutdown.store(false);
    g_loadingActive.store(false);
    
    g_defragEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_defragEvent) {
        Log("[LoadingDefrag] Failed to create worker event");
        return false;
    }
    
    g_defragThread = CreateThread(NULL, 0, DefragWorkerThread, NULL, 0, NULL);
    if (!g_defragThread) {
        Log("[LoadingDefrag] Failed to create background worker thread");
        CloseHandle(g_defragEvent);
        g_defragEvent = nullptr;
        return false;
    }
    
    Log("[LoadingDefrag] Module successfully initialized");
    return true;
}

void Shutdown() {
    g_shutdown.store(true);
    if (g_defragEvent) {
        SetEvent(g_defragEvent);
    }
    if (g_defragThread) {
        WaitForSingleObject(g_defragThread, 2000);
        CloseHandle(g_defragThread);
        g_defragThread = nullptr;
    }
    if (g_defragEvent) {
        CloseHandle(g_defragEvent);
        g_defragEvent = nullptr;
    }
    
    DeleteCriticalSection(&g_zoneLock);
    g_zoneHistory.clear();
    Log("[LoadingDefrag] Module shutdown complete");
}

void NotifyLoadingState(bool isLoading) {
    if (isLoading) {
        g_loadingActive.store(true);
        SetEvent(g_defragEvent);
    } else {
        g_loadingActive.store(false);
    }
}

// Helper to check if string matches GetStackTopFast / SetStackTopFast definitions
static inline uintptr_t GetStackTopFast(uintptr_t L) {
    return *(uintptr_t*)(L + 0x0C);
}

static inline void SetStackTopFast(uintptr_t L, uintptr_t top) {
    *(uintptr_t*)(L + 0x0C) = top;
}

static bool TryGetZoneName(uintptr_t L, char* outName, size_t outSize) {
    __try {
        if (FrameScript_Execute_) {
            FrameScript_Execute_("if GetRealZoneText then LUABOOST_CURRENT_ZONE = GetRealZoneText() else LUABOOST_CURRENT_ZONE = nil end", "loading_defrag", 0);
            
            // Read it from the stack using lua_getfield
            lua_getfield_(L, LUA_GLOBALSINDEX, "LUABOOST_CURRENT_ZONE");
            uintptr_t top = GetStackTopFast(L);
            if (top >= 0x10) {
                int tt = *(int*)(top - 8);
                if (tt == LUA_TSTRING) {
                    uintptr_t ts = *(uintptr_t*)(top - 16);
                    if (ts >= 0x10000) {
                        const char* zoneName = (const char*)(ts + 20);
                        if (zoneName && strlen(zoneName) < outSize - 1) {
                            strcpy_s(outName, outSize, zoneName);
                            SetStackTopFast(L, top - 16);
                            return true;
                        }
                    }
                }
            }
            // Pop the value from the stack to keep it clean
            SetStackTopFast(L, top - 16);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        // Safe fallback
    }
    return false;
}

static void UpdateZoneHistory(const char* zoneName) {
    EnterCriticalSection(&g_zoneLock);
    strcpy_s(g_currentZone, sizeof(g_currentZone), zoneName);
    
    // If a new zone was entered, log and record it
    if (strcmp(g_currentZone, g_lastZone) != 0) {
        Log("[LoadingDefrag] Zone transitioned: '%s' -> '%s'", g_lastZone, g_currentZone);
        strcpy_s(g_lastZone, sizeof(g_lastZone), g_currentZone);
        
        // Update history
        bool found = false;
        for (auto& zone : g_zoneHistory) {
            if (zone.name == g_currentZone) {
                zone.visitCount++;
                found = true;
                break;
            }
        }
        if (!found) {
            g_zoneHistory.push_back({g_currentZone, 1});
        }
    }
    LeaveCriticalSection(&g_zoneLock);
}

void OnFrame() {
    // Only query zone text on the main thread when we are not actively in loading screen
    if (g_loadingActive.load(std::memory_order_relaxed)) return;
    
    static DWORD lastQueryTick = 0;
    DWORD now = GetTickCount();
    if (now - lastQueryTick < 1000) return; // Limit to 1 query per second
    lastQueryTick = now;
    
    uintptr_t L = *(uintptr_t*)0x00D3F78C;
    if (L < 0x10000 || L > 0xFFE00000) return;

    char tempZone[128] = {0};
    if (TryGetZoneName(L, tempZone, sizeof(tempZone))) {
        UpdateZoneHistory(tempZone);
    }
}

} // namespace LoadingDefrag
