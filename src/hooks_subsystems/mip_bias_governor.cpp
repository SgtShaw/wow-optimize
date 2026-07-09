#include "mip_bias_governor.h"
#include "core/config.h"
#include "version.h"
#include <atomic>

namespace MipBiasGovernor {

static std::atomic<float> g_currentMipBias{0.0f};

void UpdateMipBias(double frameMs) {
    if (!Config::g_settings.OptMipBiasGovernor) return;
    #if !TEST_DISABLE_MIP_BIAS_GOVERNOR
    if (frameMs <= 0.0) return;
    
    float targetBias = 0.0f;
    if (frameMs > 22.0) {
        targetBias = 1.5f;
    } else if (frameMs > 16.6) {
        targetBias = 0.8f;
    }
    
    float current = g_currentMipBias.load(std::memory_order_relaxed);
    g_currentMipBias.store(current * 0.95f + targetBias * 0.05f, std::memory_order_relaxed);
    #endif
}

float GetCurrentBias() {
    #if !TEST_DISABLE_MIP_BIAS_GOVERNOR
    return g_currentMipBias.load(std::memory_order_relaxed);
    #else
    return 0.0f;
    #endif
}

bool Init() {
    if (!Config::g_settings.OptMipBiasGovernor) {
        return true;
    }
    g_currentMipBias.store(0.0f);
    return true;
}

void Shutdown() {
    if (!Config::g_settings.OptMipBiasGovernor) return;
}

} // namespace MipBiasGovernor
