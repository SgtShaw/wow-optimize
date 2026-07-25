#pragma once

// ============================================================================
// Module: event_coalescer.h
// ============================================================================

extern "C" void EventCoalescer_Flush();

namespace EventCoalescer {
    bool Init();
    void Shutdown();

    // True once Init() has armed the frame-scoped dedup queue. The detour itself
    // lives in LoadingState, which only routes events here while this is true.
    bool IsActive();

    // Returns true if the event was queued/dropped and must NOT reach the client.
    // Called from the LoadingState detour with the raw vararg block.
    bool TryQueue(int eventId, const char* format, void* vaStart);
}
