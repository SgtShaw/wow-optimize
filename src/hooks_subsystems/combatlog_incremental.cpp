// ============================================================================
// Module: combatlog_incremental.cpp
// Description: Batching and incremental parsing of combat log events.
// Safety & Threading: Thread-safe queue, rate-limited processing.
// ============================================================================

#include "combatlog_incremental.h"
#include "version.h"
#include <windows.h>
#include <vector>
#include <mutex>

extern "C" void Log(const char* fmt, ...);

namespace CombatLogIncremental {

struct StoredEvent {
    uint8_t data[128];
};

static std::vector<StoredEvent> g_eventQueue;
static std::mutex g_queueMutex;
static int g_eventsThisFrame = 0;
static constexpr int MAX_SYNC_EVENTS_PER_FRAME = 16;
static constexpr int MAX_DEFERRED_PROCESS_PER_FRAME = 8;
static void* g_origCombatLogEvent = nullptr;

bool Init() {
    g_origCombatLogEvent = nullptr;
    Log("[CombatLogIncremental] Active - Rate-limited combat log queue initialized.");
    return true;
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_queueMutex);
    g_eventQueue.clear();
}

typedef int (__thiscall* CombatLogEvent_fn)(void* This, int luaState);

static void SafeCallCombatLogEvent(CombatLogEvent_fn orig, void* data, int luaState) {
    __try {
        orig(data, luaState);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void OnFrame(int luaState) {
    g_eventsThisFrame = 0;

    if (!g_origCombatLogEvent || luaState < 0x10000 || luaState > 0xFFE00000) return;

    std::vector<StoredEvent> toProcess;
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        if (g_eventQueue.empty()) return;

        int count = (int)g_eventQueue.size();
        if (count > MAX_DEFERRED_PROCESS_PER_FRAME) {
            count = MAX_DEFERRED_PROCESS_PER_FRAME;
        }
        toProcess.assign(g_eventQueue.begin(), g_eventQueue.begin() + count);
        g_eventQueue.erase(g_eventQueue.begin(), g_eventQueue.begin() + count);
    }

    CombatLogEvent_fn orig = (CombatLogEvent_fn)g_origCombatLogEvent;

    for (auto& ev : toProcess) {
        SafeCallCombatLogEvent(orig, ev.data, luaState);
    }
}

bool ShouldDefer(void* This, int luaState, void* orig_func) {
    g_origCombatLogEvent = orig_func;

#if TEST_DISABLE_COMBATLOG_INCREMENTAL
    return false;
#else
    g_eventsThisFrame++;
    if (g_eventsThisFrame <= MAX_SYNC_EVENTS_PER_FRAME) {
        return false;
    }

    // Queue the event and defer it
    std::lock_guard<std::mutex> lock(g_queueMutex);
    if (g_eventQueue.size() < 1024) {
        StoredEvent ev;
        memcpy(ev.data, This, 128);
        g_eventQueue.push_back(ev);
    }
    return true;
#endif
}

} // namespace CombatLogIncremental
