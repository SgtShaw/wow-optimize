#include "addon_msg_limiter.h"
#include "version.h"
#include <unordered_map>
#include "win_mutex.h"

extern "C" void Log(const char* fmt, ...);

namespace AddonMsgLimiter {

static std::unordered_map<std::string, DWORD> g_prefixLastSent;
static WinMutex g_limiterMutex;
static uint64_t g_suppressedCount = 0;

bool Init() {
    Log("[AddonMsgLimiter] Active - Addon Message Rate Limiter & Coalescer initialized");
    return true;
}

void Shutdown() {
    WinLockGuard lock(g_limiterMutex);
    g_prefixLastSent.clear();
    Log("[AddonMsgLimiter] Stats: Suppressed %lld outbound addon sync messages", g_suppressedCount);
}

bool ShouldSendAddonMessage(const std::string& prefix, const std::string& text) {
    if (prefix.empty()) return true;
    DWORD now = GetTickCount();
    WinLockGuard lock(g_limiterMutex);
    auto it = g_prefixLastSent.find(prefix);
    if (it != g_prefixLastSent.end() && (now - it->second < 50)) { // Limit to max 20 messages per second per prefix
        g_suppressedCount++;
        return false; // Rate limit / suppress
    }
    g_prefixLastSent[prefix] = now;
    return true;
}

} // namespace AddonMsgLimiter
