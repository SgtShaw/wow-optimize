// ============================================================================
// Module: world_state_coalesce.cpp
// Description: Coalescing network/field updates to reduce redundant UI redraws.
// Safety & Threading: Thread-safe, queues updates and flushes on main thread.
// ============================================================================

#include "world_state_coalesce.h"
#include "version.h"
#include <windows.h>
#include <mutex>

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
static std::mutex g_lock;
static void* g_origOnFieldUpdate = nullptr;

bool Init() {
    std::lock_guard<std::mutex> lock(g_lock);
    g_queueCount = 0;
    for (int i = 0; i < MAX_QUEUED_UPDATES; i++) {
        g_queue[i].active = false;
    }
    Log("[WorldStateCoalesce] Active - Coalesced World State updates system ready.");
    return true;
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_lock);
    g_queueCount = 0;
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
        std::lock_guard<std::mutex> lock(g_lock);
        if (g_queueCount == 0) return;

        for (int i = 0; i < MAX_QUEUED_UPDATES; i++) {
            if (g_queue[i].active) {
                localQueue[count++] = g_queue[i];
                g_queue[i].active = false;
            }
        }
        g_queueCount = 0;
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

    std::lock_guard<std::mutex> lock(g_lock);

    // Look for existing update for same unit and fieldId
    for (int i = 0; i < MAX_QUEUED_UPDATES; i++) {
        if (g_queue[i].active && g_queue[i].unit == unit && g_queue[i].fieldId == fieldId) {
            g_queue[i].value = value; // Update with latest value
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
            return true;
        }
    }

    return false; // Queue full, let caller process immediately
#endif
}

} // namespace WorldStateCoalesce
