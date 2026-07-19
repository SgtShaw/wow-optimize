#pragma once

// ============================================================================
// Module: crash_dumper.h
// Description: Supporting utility functions for `crash_dumper.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `crash_dumper.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Memory Watchdog and Diagnostic Telemetry
 * @architecture: Implements minidump capture routines and watches CVars to log crash events.
 * @thread_affinity: Main Loop / Telemetry Threads
 * @regression_hazard: Exception handlers must not allocate memory during diagnostic recording to prevent nested exceptions.
 */


#ifndef CRASH_DUMPER_H
#define CRASH_DUMPER_H

// Feature tracking for crash diagnosis
// Each optimization registers itself so crash dumps show exactly what was active
#define MAX_TRACKED_FEATURES 128

struct FeatureState {
    const char* name;        // Feature name (e.g., "AdaptiveGC", "GetStrInline")
    bool        active;      // Currently enabled
    long long   callCount;   // Total invocations since init
    long long   errorCount;  // SEH exceptions caught
    DWORD       lastCallTick;// GetTickCount of last invocation
    const char* lastError;   // Last error description (static string)
};

namespace CrashDumper {
    bool Init();
    void Shutdown();

    // Register a tracked feature for crash diagnostics
    void RegisterFeature(const char* name);

    // Update feature state (call from hooks/fast-paths)
    void FeatureCall(const char* name);
    void FeatureError(const char* name, const char* desc);
    void FeatureSetActive(const char* name, bool active);

    // Get current feature states for crash dump
    int GetFeatureStates(FeatureState* out, int maxCount);

    // Record last hook call for crash context (ring buffer, lock-free)
    void RecordHookCall(const char* hookName, uintptr_t addr);

    // Hot-path variant for very high-frequency hooks (UI accessors etc.).
    // Samples ~1/64 calls so the per-call InterlockedIncrement stays off the
    // frame critical path, and so one hot hook can't flood the 256-slot ring
    // and evict the rarer, riskier hooks we actually want in a crash trace.
    void RecordHookCallHot(const char* hookName, uintptr_t addr);
}

#endif
