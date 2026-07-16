// ============================================================================
// Module: anim_blend_cache.cpp
// Description: Animation bone matrix caching for M2 model rendering.
// Status: DISABLED — correct target address for CM2Model::UpdateBones has not
//         been identified. The previously used address 0x5F91E0 is actually
//         AddonStateList::Load (addon persistence), not an animation function.
//         Hooking it would intercept addon state loading, corrupting saved
//         addon data and causing crashes.
//
//         To implement this properly, one must:
//         1. Find CM2Model::UpdateBones / CM2Model::CalcBoneMatrix in the
//            binary (likely in the 0x7B0xxx-0x7B5xxx range near M2 code).
//         2. Verify the calling convention and parameter layout.
//         3. Implement cache invalidation keyed on (model_ptr, anim_id, time)
//            with proper handling for model destruction/reload.
// ============================================================================

#include "anim_blend_cache.h"
#include "MinHook.h"

extern "C" void Log(const char* fmt, ...);

namespace AnimBlendCache {

    bool Init() {
        Log("[AnimBlendCache] DISABLED (correct UpdateBones address not identified; "
            "0x5F91E0 is AddonStateList::Load, not UpdateBones)");
        return false;
    }

    void Shutdown() {
        // Nothing to clean up — hook was never installed
    }

    void Clear() {
        // Nothing to clear — cache was never populated
    }

    bool GetCachedMatrix(void* /*model*/, int /*boneIndex*/, float /*animTime*/, float* /*outMatrix*/) {
        return false;
    }

    void AddToCache(void* /*model*/, int /*boneIndex*/, float /*animTime*/, const float* /*matrix*/) {
        // Stub — not active
    }
}
