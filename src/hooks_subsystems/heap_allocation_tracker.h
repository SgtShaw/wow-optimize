#pragma once
#include <windows.h>

namespace HeapAllocationTracker {
    bool Init();
    void Shutdown();
    void TrackAllocation(void* ptr, size_t size);
    void TrackFree(void* ptr);
}
