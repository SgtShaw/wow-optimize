#pragma once

// ============================================================================
// Module: dxvk_bridge.h
// Description: Supporting utility functions for `dxvk_bridge.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `dxvk_bridge.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */



// Detects DXVK / vk9 / dgVoodoo2 (Vulkan translation of D3D9). Other
// modules consult IsActive() to skip work the Vulkan driver already does.

#include <windows.h>
#include <cstdint>

namespace DXVKBridge {

bool Init();
void Shutdown();

bool IsActive();
double PresentIntervalMs();
bool   ShouldSkipGpuSync();
bool   ShouldSkipStateCache();

void NotePresent();

struct Stats {
    bool        active;
    const char* detectionReason;
    double      presentIntervalMs;
    uint64_t    presents;
};
void GetStats(Stats* out);

} // namespace DXVKBridge
