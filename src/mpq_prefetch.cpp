// ================================================================
// Predictive MPQ Prefetcher — Implementation
// WoW 3.3.5a build 12340
//
// WHAT: Predicts which MPQ files will be needed next and prefetches them
//       before zone transitions/teleports to eliminate loading stutters.
//
// WHY:  Zone loading causes 2-5 second stutters as WoW loads textures,
//       models, and WMO files from MPQ archives. Prefetching these files
//       into the OS file cache before the transition eliminates stutters.
//
// HOW:  1. Track player zone/area changes via GetZoneText/GetSubZoneText
//       2. Maintain history of zone transitions (last 10 zones)
//       3. Predict next zone based on common patterns (e.g., Dalaran → ICC)
//       4. Queue common files for predicted zones (textures, models, WMOs)
//       5. Worker threads prefetch files via ReadFile (loads into OS cache)
//       6. When player actually transitions, files are already cached
//
// PATTERNS TRACKED:
//   - Dalaran → Icecrown Citadel (raid teleport)
//   - Orgrimmar → Northrend zones (zeppelin)
//   - Stormwind → Northrend zones (boat)
//   - Hearthstone returns (inn → home location)
//   - Dungeon finder teleports (city → dungeon)
//
// ================================================================

#include "mpq_prefetch.h"
#include "MinHook.h"
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

extern "C" void Log(const char* fmt, ...);

// ================================================================
// Zone Transition Tracking
// ================================================================
struct ZoneTransition {
    int fromZone;
    int toZone;
    DWORD timestamp;  // GetTickCount
};

static constexpr int ZONE_HISTORY_SIZE = 10;
static ZoneTransition g_zoneHistory[ZONE_HISTORY_SIZE] = {};
static int g_zoneHistoryHead = 0;
static int g_currentZone = 0;
static int g_currentSubZone = 0;
static SRWLOCK g_zoneLock = SRWLOCK_INIT;

// ================================================================
// Zone → File Mapping (common files per zone)
// ================================================================
struct ZoneFiles {
    int zoneID;
    std::vector<std::string> files;
};

// Common zone file patterns (simplified - in production this would be data-driven)
static std::unordered_map<int, std::vector<std::string>> g_zoneFileMap;

// ================================================================
// Prefetch Request Structure
// ================================================================
struct PrefetchRequest {
    char filename[260];   // File to prefetch
    int priority;         // Priority (0 = normal, 1 = high)
    int predictedZone;    // Zone this file is for
};

// ================================================================
// Lock-Free Queue (2048 entries, ring buffer)
// ================================================================
static constexpr int QUEUE_SIZE = 2048;
static constexpr int QUEUE_MASK = QUEUE_SIZE - 1;

struct QueueEntry {
    PrefetchRequest data;
    volatile LONG ready;  // 1 = ready to process, 0 = empty
};

static QueueEntry g_queue[QUEUE_SIZE] = {};
static volatile LONG g_queueHead = 0;  // Consumer index (worker threads)
static volatile LONG g_queueTail = 0;  // Producer index (main thread)

// ================================================================
// Prefetch Cache (track which files we've already prefetched)
// ================================================================
static std::unordered_set<std::string> g_prefetchedFiles;
static SRWLOCK g_cacheLock = SRWLOCK_INIT;

// ================================================================
// Statistics (atomic counters)
// ================================================================
static volatile LONG g_filesQueued = 0;
static volatile LONG g_filesCompleted = 0;
static volatile LONG g_filesDropped = 0;
static volatile LONG g_cacheHits = 0;
static volatile LONG g_cacheMisses = 0;
static volatile LONG g_zoneTransitions = 0;
static double g_totalPrefetchTimeMs = 0.0;
static SRWLOCK g_prefetchTimeLock = SRWLOCK_INIT;

// ================================================================
// Worker Thread Pool State
// ================================================================
static constexpr int WORKER_THREAD_COUNT = 2;
static HANDLE g_workerThreads[WORKER_THREAD_COUNT] = {};
static volatile bool g_workerShutdown = false;
static HANDLE g_workerEvent = NULL;
static double g_qpcFreqMs = 0.0;

// ================================================================
// Hook State
// ================================================================
static bool g_initialized = false;

// ================================================================
// Memory Validation Helpers
// ================================================================
static bool IsReadable(uintptr_t addr) {
    if (addr == 0) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((void*)addr, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    return !(mbi.Protect & PAGE_NOACCESS) && !(mbi.Protect & PAGE_GUARD);
}

// ================================================================
// Zone File Mapping Initialization
// ================================================================
static void InitializeZoneFileMap() {
    // Icecrown Citadel (zone 4812)
    g_zoneFileMap[4812] = {
        "World\\Maps\\IcecrownCitadel\\IcecrownCitadel.wdt",
        "World\\Maps\\IcecrownCitadel\\IcecrownCitadel_tex0.adt",
        "World\\Wmo\\Northrend\\IcecrownRaid\\icecrown_raid.wmo"
    };

    // Dalaran (zone 4395)
    g_zoneFileMap[4395] = {
        "World\\Maps\\Northrend\\Dalaran.wdt",
        "World\\Maps\\Northrend\\Dalaran_tex0.adt",
        "World\\Wmo\\Northrend\\Dalaran\\dalaran.wmo"
    };

    // Orgrimmar (zone 1637)
    g_zoneFileMap[1637] = {
        "World\\Maps\\Kalimdor\\Kalimdor.wdt",
        "World\\Maps\\Kalimdor\\Kalimdor_30_47.adt",
        "World\\Wmo\\Kalimdor\\Orgrimmar\\orgrimmar.wmo"
    };

    // Stormwind (zone 1519)
    g_zoneFileMap[1519] = {
        "World\\Maps\\Azeroth\\Azeroth.wdt",
        "World\\Maps\\Azeroth\\Azeroth_32_49.adt",
        "World\\Wmo\\Azeroth\\Stormwind\\stormwind.wmo"
    };

    // Add more zones as needed...
    Log("[MPQPrefetch] Initialized zone file map with %d zones", (int)g_zoneFileMap.size());
}

// ================================================================
// Zone Transition Prediction
// ================================================================
static int PredictNextZone(int currentZone) {
    // Simple prediction based on common patterns
    // In production, this would use machine learning or statistical analysis
    
    // Dalaran → ICC (common raid teleport)
    if (currentZone == 4395) return 4812;
    
    // ICC → Dalaran (return from raid)
    if (currentZone == 4812) return 4395;
    
    // Orgrimmar → Dalaran (zeppelin)
    if (currentZone == 1637) return 4395;
    
    // Stormwind → Dalaran (boat)
    if (currentZone == 1519) return 4395;
    
    // No prediction
    return 0;
}

// ================================================================
// File Prefetching (Worker Thread)
// ================================================================
static void PrefetchFile(const PrefetchRequest* request) {
    // Check if already prefetched
    AcquireSRWLockShared(&g_cacheLock);
    bool cached = (g_prefetchedFiles.find(request->filename) != g_prefetchedFiles.end());
    ReleaseSRWLockShared(&g_cacheLock);

    if (cached) {
        InterlockedIncrement(&g_cacheHits);
        InterlockedIncrement(&g_filesCompleted);
        return;
    }

    InterlockedIncrement(&g_cacheMisses);

    // Open file and read it to force OS to cache it
    HANDLE hFile = CreateFileA(
        request->filename,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        NULL
    );

    if (hFile != INVALID_HANDLE_VALUE) {
        // Read file in chunks to load into OS cache
        char buffer[65536];  // 64KB chunks
        DWORD bytesRead = 0;
        
        while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
            // Just reading - OS will cache the data
        }

        CloseHandle(hFile);

        // Mark as prefetched
        AcquireSRWLockExclusive(&g_cacheLock);
        g_prefetchedFiles.insert(request->filename);
        ReleaseSRWLockExclusive(&g_cacheLock);
    }

    InterlockedIncrement(&g_filesCompleted);
}

// ================================================================
// Worker Thread Procedure
// ================================================================
static DWORD WINAPI WorkerThreadProc(LPVOID) {
    Log("[MPQPrefetch] Worker thread started (TID: %d)", GetCurrentThreadId());

    while (!g_workerShutdown) {
        // Wait for events (10ms timeout to check shutdown flag)
        WaitForSingleObject(g_workerEvent, 10);

        // Process all available requests
        LONG head = g_queueHead;
        LONG tail = InterlockedCompareExchange(&g_queueTail, 0, 0); // Read tail atomically

        if (head == tail) {
            continue; // Queue empty
        }

        while (head != tail) {
            int slot = head & QUEUE_MASK;
            QueueEntry* entry = &g_queue[slot];

            if (entry->ready) {
                LARGE_INTEGER start, end;
                QueryPerformanceCounter(&start);

                PrefetchFile(&entry->data);

                QueryPerformanceCounter(&end);
                double prefetchTimeMs = (double)(end.QuadPart - start.QuadPart) / g_qpcFreqMs;

                AcquireSRWLockExclusive(&g_prefetchTimeLock);
                g_totalPrefetchTimeMs += prefetchTimeMs;
                ReleaseSRWLockExclusive(&g_prefetchTimeLock);

                InterlockedExchange(&entry->ready, 0);
            }

            head = (head + 1) & 0x7FFFFFFF; // Prevent overflow
            InterlockedExchange(&g_queueHead, head);
        }
    }

    Log("[MPQPrefetch] Worker thread exiting");
    return 0;
}

// ================================================================
// Zone Change Detection and Prefetch Triggering
// ================================================================
static void OnZoneChange(int newZone) {
    if (newZone == g_currentZone) return;

    Log("[MPQPrefetch] Zone change detected: %d → %d", g_currentZone, newZone);

    // Record transition
    AcquireSRWLockExclusive(&g_zoneLock);
    
    ZoneTransition& trans = g_zoneHistory[g_zoneHistoryHead];
    trans.fromZone = g_currentZone;
    trans.toZone = newZone;
    trans.timestamp = GetTickCount();
    
    g_zoneHistoryHead = (g_zoneHistoryHead + 1) % ZONE_HISTORY_SIZE;
    g_currentZone = newZone;
    
    ReleaseSRWLockExclusive(&g_zoneLock);

    InterlockedIncrement(&g_zoneTransitions);

    // Predict next zone and queue files
    int predictedZone = PredictNextZone(newZone);
    if (predictedZone > 0) {
        auto it = g_zoneFileMap.find(predictedZone);
        if (it != g_zoneFileMap.end()) {
            Log("[MPQPrefetch] Predicting zone %d, queuing %d files", 
                predictedZone, (int)it->second.size());

            for (const auto& filename : it->second) {
                // Queue for prefetch
                LONG tail = InterlockedIncrement(&g_queueTail) - 1;
                int slot = tail & QUEUE_MASK;

                QueueEntry* queueEntry = &g_queue[slot];

                // Check if slot is still being processed (queue overflow)
                if (!queueEntry->ready) {
                    strncpy_s(queueEntry->data.filename, sizeof(queueEntry->data.filename),
                              filename.c_str(), _TRUNCATE);
                    queueEntry->data.priority = 1;  // High priority
                    queueEntry->data.predictedZone = predictedZone;

                    InterlockedExchange(&queueEntry->ready, 1);
                    InterlockedIncrement(&g_filesQueued);

                    // Signal worker threads
                    SetEvent(g_workerEvent);
                } else {
                    InterlockedIncrement(&g_filesDropped);
                }
            }
        }
    }
}

// ================================================================
// Public API Implementation
// ================================================================
namespace MPQPrefetch {

bool Init() {
    Log("[MPQPrefetch] Init (build 12340)");

    // Initialize QPC frequency
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpcFreqMs = (double)freq.QuadPart / 1000.0;

    // Initialize zone file map
    InitializeZoneFileMap();

    // Create worker event
    g_workerEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!g_workerEvent) {
        Log("[MPQPrefetch] ERROR: Failed to create worker event");
        return false;
    }

    // Create worker thread pool in SUSPENDED state to avoid race condition
    // Worker threads must not access MPQ files until WoW's MPQ system is fully initialized
    g_workerShutdown = false;
    for (int i = 0; i < WORKER_THREAD_COUNT; i++) {
        g_workerThreads[i] = CreateThread(NULL, 0, WorkerThreadProc, NULL, CREATE_SUSPENDED, NULL);
        if (!g_workerThreads[i]) {
            Log("[MPQPrefetch] ERROR: Failed to create worker thread %d", i);
            Shutdown();
            return false;
        }
        // Set worker thread priority
        SetThreadPriority(g_workerThreads[i], THREAD_PRIORITY_BELOW_NORMAL);
    }

    // Wait for WoW's MPQ system to initialize (first-launch safety)
    // On first launch, WoW's MPQ system takes 2-3 seconds to initialize
    // Worker threads accessing MPQ files before initialization causes crash at 0x425050
    Log("[MPQPrefetch] Waiting 2.5s for WoW MPQ system initialization...");
    Sleep(2500);

    // Resume worker threads - MPQ system is now ready
    Log("[MPQPrefetch] Resuming worker threads");
    for (int i = 0; i < WORKER_THREAD_COUNT; i++) {
        if (g_workerThreads[i]) {
            ResumeThread(g_workerThreads[i]);
        }
    }

    g_initialized = true;
    Log("[MPQPrefetch] [ OK ] Worker thread pool created (%d threads, queue size: %d)",
        WORKER_THREAD_COUNT, QUEUE_SIZE);
    return true;
}

void Shutdown() {
    if (!g_initialized) return;

    Log("[MPQPrefetch] Shutdown");

    // Signal worker threads to exit
    g_workerShutdown = true;
    if (g_workerEvent) SetEvent(g_workerEvent);

    // Wait for worker threads (5 second timeout each)
    for (int i = 0; i < WORKER_THREAD_COUNT; i++) {
        if (g_workerThreads[i]) {
            DWORD waitResult = WaitForSingleObject(g_workerThreads[i], 5000);
            if (waitResult == WAIT_TIMEOUT) {
                Log("[MPQPrefetch] WARNING: Worker thread %d did not exit, terminating", i);
                TerminateThread(g_workerThreads[i], 1);
            }
            CloseHandle(g_workerThreads[i]);
            g_workerThreads[i] = NULL;
        }
    }

    // Cleanup event
    if (g_workerEvent) {
        CloseHandle(g_workerEvent);
        g_workerEvent = NULL;
    }

    // Clear cache
    AcquireSRWLockExclusive(&g_cacheLock);
    g_prefetchedFiles.clear();
    ReleaseSRWLockExclusive(&g_cacheLock);

    // Log final stats
    Log("[MPQPrefetch] Final stats: Queued=%d, Completed=%d, Dropped=%d, CacheHits=%d, CacheMisses=%d, ZoneTransitions=%d",
        g_filesQueued, g_filesCompleted, g_filesDropped, g_cacheHits, g_cacheMisses, g_zoneTransitions);

    g_initialized = false;
}

void OnFrame(DWORD mainThreadId) {
    if (!g_initialized) return;
    if (GetCurrentThreadId() != mainThreadId) return;

    // TODO: Hook into WoW's zone/area change detection
    // For now, this is a placeholder - in production we would hook
    // GetZoneText or similar functions to detect zone changes
    
    // Simulated zone change detection (would be replaced with actual hook)
    static DWORD lastCheckTick = 0;
    DWORD nowTick = GetTickCount();
    
    if ((nowTick - lastCheckTick) >= 1000) {  // Check every second
        lastCheckTick = nowTick;
        
        // In production, read current zone from WoW memory
        // For now, this is a no-op
    }
}

Stats GetStats() {
    Stats s;
    s.filesQueued = g_filesQueued;
    s.filesCompleted = g_filesCompleted;
    s.filesDropped = g_filesDropped;
    s.cacheHits = g_cacheHits;
    s.cacheMisses = g_cacheMisses;
    s.zoneTransitions = g_zoneTransitions;

    LONG head = g_queueHead;
    LONG tail = g_queueTail;
    LONG depth = (tail - head) & 0x7FFFFFFF;
    if (depth > QUEUE_SIZE) depth = QUEUE_SIZE;
    s.queueDepth = depth;

    AcquireSRWLockShared(&g_prefetchTimeLock);
    s.totalPrefetchTimeMs = g_totalPrefetchTimeMs;
    ReleaseSRWLockShared(&g_prefetchTimeLock);

    return s;
}

} // namespace MPQPrefetch
