#include "addon_tick_governor.h"
#include "version.h"
#include <cstring>
#include <atomic>

extern "C" void Log(const char* fmt, ...);

namespace AddonTickGovernor {

struct AddonSlot {
    char name[32];
    DWORD lastTick;
    bool nonCritical;
};

static constexpr int MAX_ADDONS = 64;
static AddonSlot g_addons[MAX_ADDONS] = {};
static std::atomic<int> g_addonCount{0};

static const char* g_nonCriticalAddons[] = {
    "details",
    "recount",
    "skada",
    "auctionator",
    "auctioneer",
    "questhelper",
    "gatherer",
    "carbonite",
    "postal",
    "bagnon",
    "arkinventory",
    "atlasloot",
    "overachiever",
    "wim",
    "chatter",
    "prat",
    nullptr
};

static bool IsNonCritical(const char* name) {
    for (int i = 0; g_nonCriticalAddons[i]; i++) {
        if (_stricmp(name, g_nonCriticalAddons[i]) == 0) {
            return true;
        }
    }
    return false;
}

static inline const char* ExtractAddonName(const char* source, char* outBuffer, int maxLen) {
    const char* p = strstr(source, "AddOns\\");
    if (!p) p = strstr(source, "addons\\");
    if (!p) return nullptr;
    
    p += 7;
    int len = 0;
    while (p[len] && p[len] != '\\' && p[len] != '/' && len < maxLen - 1) {
        outBuffer[len] = p[len];
        len++;
    }
    outBuffer[len] = '\0';
    return outBuffer;
}

bool ShouldThrottle(const char* source) {
    #if TEST_DISABLE_ADDON_TICK_GOVERNOR
    return false;
    #else
    if (!source || !*source) return false;
    
    char name[32];
    if (!ExtractAddonName(source, name, sizeof(name))) return false;
    
    DWORD now = GetTickCount();
    
    int count = g_addonCount.load(std::memory_order_relaxed);
    for (int i = 0; i < count; i++) {
        if (_stricmp(g_addons[i].name, name) == 0) {
            if (g_addons[i].nonCritical) {
                if (now - g_addons[i].lastTick < 50) {
                    return true;
                }
                g_addons[i].lastTick = now;
            }
            return false;
        }
    }
    
    if (count < MAX_ADDONS) {
        int idx = g_addonCount.fetch_add(1, std::memory_order_relaxed);
        if (idx < MAX_ADDONS) {
            strncpy_s(g_addons[idx].name, sizeof(g_addons[idx].name), name, _TRUNCATE);
            g_addons[idx].lastTick = now;
            g_addons[idx].nonCritical = IsNonCritical(name);
            if (g_addons[idx].nonCritical) {
                Log("[AddonTickGovernor] Registered non-critical addon: %s (throttled to 20Hz)", name);
            }
        }
    }
    
    return false;
    #endif
}

bool Init() {
    g_addonCount.store(0);
    Log("[AddonTickGovernor] Adaptive Addon Tick Governor Initialized");
    return true;
}

void Shutdown() {
}

} // namespace AddonTickGovernor
