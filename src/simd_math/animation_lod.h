#pragma once

// Animation LOD: throttle skeletal bone updates for background models in crowded
// scenes. See animation_lod.cpp for the full rationale and safety argument.
namespace AnimationLod {
    bool Init();
    void LogStats();   // call from the periodic stats dump
    void Shutdown();
}
