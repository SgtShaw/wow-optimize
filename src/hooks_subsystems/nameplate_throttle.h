#pragma once

// Nameplate update throttle: skip the per-nameplate per-frame update for
// non-target nameplates in crowded scenes (BGs/cities). See nameplate_throttle.cpp
// for the full rationale and safety argument. Self-gates on OptNameplateThrottle.
namespace NameplateThrottle {
    bool Init();
    void LogStats();   // call from the periodic stats dump
    void Shutdown();
}
