#include "m2_lod_bias.h"
#include "MinHook.h"
#include "version.h"
#include "spatial_culling.h"
#include <atomic>

extern "C" void Log(const char* fmt, ...);

namespace M2LodBias {

typedef int (__thiscall *GetLodLevel_fn)(void* This, float distance);
static GetLodLevel_fn orig_GetLodLevel = nullptr;

static std::atomic<float> g_lodBias{1.0f};

void UpdateLodBias(double frameMs) {
    if (frameMs <= 0.0) return;
    
    float targetBias = 1.0f;
    if (frameMs > 20.0) {
        targetBias = 1.8f;
    } else if (frameMs > 16.6) {
        targetBias = 1.4f;
    } else if (frameMs < 10.0) {
        targetBias = 0.9f;
    }
    
    float current = g_lodBias.load(std::memory_order_relaxed);
    g_lodBias.store(current * 0.95f + targetBias * 0.05f, std::memory_order_relaxed);
}

static int __fastcall Hooked_GetLodLevel(void* This, void* unused_edx, float distance) {
    #if !TEST_DISABLE_M2_LOD_BIAS
    float bias = g_lodBias.load(std::memory_order_relaxed);
    #if !TEST_DISABLE_SPATIAL_CULLING
    bias *= SpatialCulling::GetSpatialCullBias(This, distance);
    #endif
    distance *= bias;
    #endif
    return orig_GetLodLevel(This, distance);
}

bool Init() {
#if TEST_DISABLE_M2_LOD_BIAS
    return true;
#endif
    void* target = (void*)0x007CD3F0;
    
    unsigned char prologue[3];
    __try {
        memcpy(prologue, target, 3);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("[M2LodBias] Target 0x007CD3F0 not readable.");
        return true;
    }

    if (prologue[0] != 0x55 || prologue[1] != 0x8B || prologue[2] != 0xEC) {
        Log("[M2LodBias] BAD PROLOGUE at 0x007CD3F0. Skipping hook.");
        return true;
    }

    if (MH_CreateHook(target, (void*)Hooked_GetLodLevel, (void**)&orig_GetLodLevel) == MH_OK) {
        if (MH_EnableHook(target) == MH_OK) {
            Log("[M2LodBias] Detour active at 0x007CD3F0.");
            return true;
        }
        MH_RemoveHook(target);
    }
    return false;
}

void Shutdown() {
#if !TEST_DISABLE_M2_LOD_BIAS
    MH_DisableHook((void*)0x007CD3F0);
#endif
}

} // namespace M2LodBias
