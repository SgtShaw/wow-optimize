#include <windows.h>
#include <string>
#include <vector>
#include "win_mutex.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

namespace CombatTextCoalescer {

struct MessageEntry {
    std::string text;
    DWORD timestamp;
    int count;
};

static std::vector<MessageEntry> g_history;
static WinMutex g_historyMutex;
static uint64_t g_coalescedCount = 0;

// Returns true if message should be coalesced / skipped
bool ProcessMessage(const std::string& text, std::string& outNewText) {
    WinLockGuard lock(g_historyMutex);
    DWORD now = GetTickCount();

    // Clean up history older than 500ms
    for (auto it = g_history.begin(); it != g_history.end(); ) {
        if (now - it->timestamp > 500) {
            it = g_history.erase(it);
        } else {
            ++it;
        }
    }

    // Check if we can coalesce this message with one in the recent history
    for (auto& entry : g_history) {
        if (entry.text == text) {
            entry.count++;
            entry.timestamp = now;
            g_coalescedCount++;
            
            char buf[32];
            sprintf(buf, " x%d", entry.count);
            outNewText = text + buf;
            return true;
        }
    }

    // Insert new message
    g_history.push_back({ text, now, 1 });
    outNewText = text;
    return false;
}

bool Init() {
    Log("[CombatTextCoalescer] Active - Combat text coalescer initialized");
    return true;
}

void Shutdown() {
    WinLockGuard lock(g_historyMutex);
    g_history.clear();
    Log("[CombatTextCoalescer] Stats: Coalesced %lld combat text messages", g_coalescedCount);
}

} // namespace CombatTextCoalescer
