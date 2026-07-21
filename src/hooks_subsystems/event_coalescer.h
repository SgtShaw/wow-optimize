#pragma once

// ============================================================================
// Module: event_coalescer.h
// ============================================================================










extern "C" void EventCoalescer_Flush();

namespace EventCoalescer {
    bool Init();
    void Shutdown();
}
