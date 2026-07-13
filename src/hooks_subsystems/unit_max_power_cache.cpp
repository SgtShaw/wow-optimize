#include "unit_max_power_cache.h"
#include <unordered_map>
#include "win_mutex.h"

namespace UnitMaxPowerCache {

struct PowerKey {
    void* unit;
    int type;

    bool operator==(const PowerKey& o) const {
        return unit == o.unit && type == o.type;
    }
};

struct PowerKeyHash {
    size_t operator()(const PowerKey& k) const {
        return (size_t)k.unit ^ k.type;
    }
};

static std::unordered_map<PowerKey, int, PowerKeyHash> g_powerCache;
static WinMutex g_powerMutex;
static uint64_t g_hits = 0;

bool Init() {
    return true;
}

void Shutdown() {
    WinLockGuard lock(g_powerMutex);
    g_powerCache.clear();
}

int GetMaxPower(void* unitObj, int powerType) {
    if (!unitObj) return -1;
    WinLockGuard lock(g_powerMutex);
    auto it = g_powerCache.find({ unitObj, powerType });
    if (it != g_powerCache.end()) {
        g_hits++;
        return it->second;
    }
    return -1;
}

void SetMaxPower(void* unitObj, int powerType, int val) {
    if (!unitObj) return;
    WinLockGuard lock(g_powerMutex);
    g_powerCache[{ unitObj, powerType }] = val;
}

} // namespace UnitMaxPowerCache
