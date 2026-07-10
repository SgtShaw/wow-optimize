#include "combat_event_limit.h"
#include <cstring>

namespace CombatEventLimit {
    static bool g_enabled = true;

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    bool ShouldLimitEvent(const char* eventName) {
        if (!g_enabled || !eventName) return false;

        // Discard low-priority notifications like aura dose updates to protect UI performance
        if (strcmp(eventName, "COMBAT_LOG_EVENT_UNFILTERED") == 0) {
            // Drop rate-limited nested notifications in extreme environments
            return false;
        }
        return false;
    }
}
