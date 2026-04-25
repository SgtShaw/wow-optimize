// ================================================================
// Multithreaded Combat Log Parser — Implementation
// WoW 3.3.5a build 12340
// ================================================================

#include "combatlog_mt.h"
#include "MinHook.h"
#include <cstdio>
#include <cstring>
#include <intrin.h>

extern "C" void Log(const char* fmt, ...);

// ================================================================
// Combat Log Entry Structure (from IDA Pro analysis)
// ================================================================
struct CombatLogEntry {
    void* next;           // +0x00: next entry in linked list
    void* prev;           // +0x04: previous entry in linked list  
    int timestamp;        // +0x08: event timestamp
    int eventType;        // +0x0C: event type ID
    uint64_t sourceGUID;  // +0x10: source GUID
    uint64_t targetGUID;  // +0x18: target GUID
    int spellID;          // +0x20: spell ID
    int amount;           // +0x24: damage/heal amount
    // ... more fields, but we only need the first few for parsing
};

// ================================================================
// Lock-Free Queue (4096 entries, ring buffer)
// ================================================================
static constexpr int QUEUE_SIZE = 4096;
static constexpr int QUEUE_MASK = QUEUE_SIZE - 1;

struct QueueEntry {
    CombatLogEntry data;
    volatile LONG ready;  // 1 = ready to process, 0 = empty
};

static QueueEntry g_queue[QUEUE_SIZE] = {};
static volatile LONG g_queueHead = 0;  // Consumer index (worker thread)
static volatile LONG g_queueTail = 0;  // Producer index (main thread)

// ================================================================
// Statistics (atomic counters)
// ================================================================
static volatile LONG g_eventsQueued = 0;
static volatile LONG g_eventsProcessed = 0;
static volatile LONG g_eventsDropped = 0;
static volatile LONG g_eventsInvalid = 0;
static double g_totalParseTimeMs = 0.0;
static SRWLOCK g_parseTimeLock = SRWLOCK_INIT;

// ================================================================
// Worker Thread State
// ================================================================
static HANDLE g_workerThread = NULL;
static volatile bool g_workerShutdown = false;
static HANDLE g_workerEvent = NULL;
static double g_qpcFreqMs = 0.0;

// ================================================================
// Hook State
// ================================================================
typedef int (__cdecl *DispatchEvents_fn)();
static DispatchEvents_fn orig_DispatchEvents = nullptr;
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

static bool IsExecutable(uintptr_t addr) {
    if (addr == 0) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((void*)addr, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

// ================================================================
// Event Processing (Worker Thread)
// ================================================================
static void ProcessEvent(const CombatLogEntry* entry) {
    __try {
        // Validate pointers before dereferencing
        if (!IsReadable((uintptr_t)entry)) {
            InterlockedIncrement(&g_eventsInvalid);
            return;
        }

        // Extract event data (simple validation for now)
        int eventType = entry->eventType;
        int timestamp = entry->timestamp;
        uint64_t sourceGUID = entry->sourceGUID;
        uint64_t targetGUID = entry->targetGUID;
        int spellID = entry->spellID;
        int amount = entry->amount;

        // Format event string (simplified for now - in production this would
        // dispatch to Lua addons or format for COMBAT_LOG_EVENT)
        // For now we just validate the data is readable
        (void)eventType;
        (void)timestamp;
        (void)sourceGUID;
        (void)targetGUID;
        (void)spellID;
        (void)amount;

        InterlockedIncrement(&g_eventsProcessed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedIncrement(&g_eventsInvalid);
    }
}

// ================================================================
// Worker Thread Procedure
// ================================================================
static DWORD WINAPI WorkerThreadProc(LPVOID) {
    Log("[CombatLogMT] Worker thread started (TID: %d)", GetCurrentThreadId());

    while (!g_workerShutdown) {
        // Wait for events (1ms timeout to check shutdown flag)
        WaitForSingleObject(g_workerEvent, 1);

        // Process all available events
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

                ProcessEvent(&entry->data);

                QueryPerformanceCounter(&end);
                double parseTimeMs = (double)(end.QuadPart - start.QuadPart) / g_qpcFreqMs;

                AcquireSRWLockExclusive(&g_parseTimeLock);
                g_totalParseTimeMs += parseTimeMs;
                ReleaseSRWLockExclusive(&g_parseTimeLock);

                InterlockedExchange(&entry->ready, 0);
            }

            head = (head + 1) & 0x7FFFFFFF; // Prevent overflow
            InterlockedExchange(&g_queueHead, head);
        }
    }

    Log("[CombatLogMT] Worker thread exiting");
    return 0;
}

// ================================================================
// Hooked Function: sub_74F910 (Combat Log Event Dispatcher)
// ================================================================
static int __cdecl Hooked_DispatchEvents() {
    // Call original function first - let WoW dispatch events normally
    int result = orig_DispatchEvents();

    __try {
        // Read from the combat log linked list (0x00ADB97C = ActiveListHead)
        uintptr_t listHead = 0x00ADB97C;
        if (!IsReadable(listHead)) {
            return result;
        }

        uintptr_t current = *(uintptr_t*)listHead;
        
        // Traverse the list and queue events for background processing
        while (current && IsReadable(current)) {
            // Check if this is a valid entry (not a sentinel)
            if ((current & 1) != 0) break;
            
            CombatLogEntry* entry = (CombatLogEntry*)current;
            
            // Copy entry data to queue
            LONG tail = InterlockedIncrement(&g_queueTail) - 1;
            int slot = tail & QUEUE_MASK;

            QueueEntry* queueEntry = &g_queue[slot];

            // Check if slot is still being processed (queue overflow)
            if (queueEntry->ready) {
                InterlockedIncrement(&g_eventsDropped);
                break;
            }

            // Copy combat log entry data (only the fields we need)
            memcpy(&queueEntry->data, entry, sizeof(CombatLogEntry));
            InterlockedExchange(&queueEntry->ready, 1);
            InterlockedIncrement(&g_eventsQueued);

            // Signal worker thread
            SetEvent(g_workerEvent);

            // Move to next entry
            current = *(uintptr_t*)(current + 4); // next pointer at offset +0x04
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedIncrement(&g_eventsInvalid);
    }

    return result;
}

// ================================================================
// Public API Implementation
// ================================================================
namespace CombatLogMT {

bool Init() {
    Log("[CombatLogMT] Init (build 12340)");

    // Initialize QPC frequency
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpcFreqMs = (double)freq.QuadPart / 1000.0;

    // Validate target address (sub_74F910 - event dispatcher)
    uintptr_t targetAddr = 0x0074F910;
    if (!IsExecutable(targetAddr)) {
        Log("[CombatLogMT] ERROR: Target address 0x%08X is not executable", targetAddr);
        return false;
    }

    // Create worker event
    g_workerEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!g_workerEvent) {
        Log("[CombatLogMT] ERROR: Failed to create worker event");
        return false;
    }

    // Create worker thread
    g_workerShutdown = false;
    g_workerThread = CreateThread(NULL, 0, WorkerThreadProc, NULL, 0, NULL);
    if (!g_workerThread) {
        Log("[CombatLogMT] ERROR: Failed to create worker thread");
        CloseHandle(g_workerEvent);
        g_workerEvent = NULL;
        return false;
    }

    // Set worker thread priority
    SetThreadPriority(g_workerThread, THREAD_PRIORITY_BELOW_NORMAL);

    // Install hook
    void* target = (void*)targetAddr;
    if (MH_CreateHook(target, (void*)Hooked_DispatchEvents, (void**)&orig_DispatchEvents) != MH_OK) {
        Log("[CombatLogMT] ERROR: Failed to create hook");
        Shutdown();
        return false;
    }

    if (MH_EnableHook(target) != MH_OK) {
        Log("[CombatLogMT] ERROR: Failed to enable hook");
        MH_RemoveHook(target);
        Shutdown();
        return false;
    }

    g_initialized = true;
    Log("[CombatLogMT] [ OK ] Hook installed at 0x%08X (event dispatcher)", targetAddr);
    Log("[CombatLogMT] [ OK ] Worker thread created (queue size: %d)", QUEUE_SIZE);
    return true;
}

void Shutdown() {
    if (!g_initialized) return;

    Log("[CombatLogMT] Shutdown");

    // Signal worker thread to exit
    g_workerShutdown = true;
    if (g_workerEvent) SetEvent(g_workerEvent);

    // Wait for worker thread (5 second timeout)
    if (g_workerThread) {
        DWORD waitResult = WaitForSingleObject(g_workerThread, 5000);
        if (waitResult == WAIT_TIMEOUT) {
            Log("[CombatLogMT] WARNING: Worker thread did not exit, terminating");
            TerminateThread(g_workerThread, 1);
        }
        CloseHandle(g_workerThread);
        g_workerThread = NULL;
    }

    // Cleanup event
    if (g_workerEvent) {
        CloseHandle(g_workerEvent);
        g_workerEvent = NULL;
    }

    // Remove hook
    MH_DisableHook((void*)0x0074F910);
    MH_RemoveHook((void*)0x0074F910);

    // Log final stats
    Log("[CombatLogMT] Final stats: Queued=%d, Processed=%d, Dropped=%d, Invalid=%d",
        g_eventsQueued, g_eventsProcessed, g_eventsDropped, g_eventsInvalid);

    g_initialized = false;
}

void OnFrame(DWORD mainThreadId) {
    if (!g_initialized) return;
    if (GetCurrentThreadId() != mainThreadId) return;

    // Update queue depth stat
    LONG head = g_queueHead;
    LONG tail = g_queueTail;
    LONG depth = (tail - head) & 0x7FFFFFFF;
    if (depth > QUEUE_SIZE) depth = QUEUE_SIZE;
    // Note: We don't use InterlockedExchange here because it's just a stat
    // and approximate value is fine
}

Stats GetStats() {
    Stats s;
    s.eventsQueued = g_eventsQueued;
    s.eventsProcessed = g_eventsProcessed;
    s.eventsDropped = g_eventsDropped;
    s.eventsInvalid = g_eventsInvalid;
    
    LONG head = g_queueHead;
    LONG tail = g_queueTail;
    LONG depth = (tail - head) & 0x7FFFFFFF;
    if (depth > QUEUE_SIZE) depth = QUEUE_SIZE;
    s.queueDepth = depth;

    AcquireSRWLockShared(&g_parseTimeLock);
    s.totalParseTimeMs = g_totalParseTimeMs;
    ReleaseSRWLockShared(&g_parseTimeLock);

    return s;
}

} // namespace CombatLogMT
