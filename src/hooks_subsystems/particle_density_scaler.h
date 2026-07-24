#pragma once
#include <windows.h>

namespace ParticleDensityScaler {
    bool Init();
    void Shutdown();
    void OnFrame(float elapsedMs);

    // Feature 24 (0x00859160): Throttled Particle Density Scaler
    bool OptimizeSub859160_ParticleThrottle(int emitterId, float fps);
}
