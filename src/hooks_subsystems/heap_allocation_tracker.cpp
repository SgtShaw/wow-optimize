#include "heap_allocation_tracker.h"
#include <atomic>

namespace HeapAllocationTracker {
    static bool g_enabled = true;
    static std::atomic<size_t> g_allocatedBytes{0};

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    void TrackAllocation(void* ptr, size_t size) {
        if (!g_enabled || !ptr) return;
        g_allocatedBytes.fetch_add(size, std::memory_order_relaxed);
    }

    void TrackFree(void* ptr) {
        // No-op (simplified tracker to keep it ultra lightweight)
    }
}
