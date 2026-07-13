#include "world_state_coalesce.h"
#include "version.h"
#include <windows.h>

extern "C" void Log(const char* fmt, ...);

namespace WorldStateCoalesce {

struct QueuedUpdate {
    void* unit;
    int   fieldId;
    int   value;
    bool  active;
};

static constexpr int MAX_QUEUED_UPDATES = 1024;
static QueuedUpdate g_queue[MAX_QUEUED_UPDATES];
static int g_queueCount = 0;
static SRWLOCK g_coalesceLock = SRWLOCK_INIT;
static void* g_origOnFieldUpdate = nullptr;

bool Init() {
    AcquireSRWLockExclusive(&g_coalesceLock);
    g_queueCount = 0;
    for (int i = 0; i < MAX_QUEUED_UPDATES; i++) {
        g_queue[i].active = false;
    }
    ReleaseSRWLockExclusive(&g_coalesceLock);
    Log("[WorldStateCoalesce] Active - Coalesced World State updates system ready.");
    return true;
}

void Shutdown() {
    AcquireSRWLockExclusive(&g_coalesceLock);
    g_queueCount = 0;
    ReleaseSRWLockExclusive(&g_coalesceLock);
}

typedef void (__fastcall *OnFieldUpdate_fn)(void* This, void* unused, int fieldId, int value);

static void SafeCallOnFieldUpdate(OnFieldUpdate_fn orig, void* unit, int fieldId, int value) {
    __try {
        orig(unit, nullptr, fieldId, value);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void OnFrame() {
    if (!g_origOnFieldUpdate) return;

    OnFieldUpdate_fn orig = (OnFieldUpdate_fn)g_origOnFieldUpdate;

    int count = 0;
    QueuedUpdate localQueue[MAX_QUEUED_UPDATES];
    {
        AcquireSRWLockExclusive(&g_coalesceLock);
        if (g_queueCount == 0) {
            ReleaseSRWLockExclusive(&g_coalesceLock);
            return;
        }

        for (int i = 0; i < MAX_QUEUED_UPDATES; i++) {
            if (g_queue[i].active) {
                localQueue[count++] = g_queue[i];
                g_queue[i].active = false;
            }
        }
        g_queueCount = 0;
        ReleaseSRWLockExclusive(&g_coalesceLock);
    }

    // Flush all coalesced updates once on main thread
    for (int i = 0; i < count; i++) {
        uintptr_t p = (uintptr_t)localQueue[i].unit;
        if (p > 0x10000 && p < 0xFFE00000) {
            SafeCallOnFieldUpdate(orig, localQueue[i].unit, localQueue[i].fieldId, localQueue[i].value);
        }
    }
}

bool ProcessFieldUpdate(void* unit, int fieldId, int value, void* orig_func) {
    g_origOnFieldUpdate = orig_func;

#if TEST_DISABLE_WORLD_STATE_COALESCE
    return false; // Let caller process immediately
#else
    // Coalesce critical fields (health, power, level) which are < 0x40
    if (fieldId >= 0x40) {
        return false;
    }

    AcquireSRWLockExclusive(&g_coalesceLock);

    // Look for existing update for same unit and fieldId
    for (int i = 0; i < MAX_QUEUED_UPDATES; i++) {
        if (g_queue[i].active && g_queue[i].unit == unit && g_queue[i].fieldId == fieldId) {
            g_queue[i].value = value; // Update with latest value
            ReleaseSRWLockExclusive(&g_coalesceLock);
            return true;
        }
    }

    // Find free slot
    for (int i = 0; i < MAX_QUEUED_UPDATES; i++) {
        if (!g_queue[i].active) {
            g_queue[i].unit = unit;
            g_queue[i].fieldId = fieldId;
            g_queue[i].value = value;
            g_queue[i].active = true;
            g_queueCount++;
            ReleaseSRWLockExclusive(&g_coalesceLock);
            return true;
        }
    }

    ReleaseSRWLockExclusive(&g_coalesceLock);
    return false; // Queue full, let caller process immediately
#endif
}

} // namespace WorldStateCoalesce
