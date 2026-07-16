// ============================================================================
// Module: movement_smoothing.cpp
// Description: Movement interpolation for smoother unit position updates.
// Status: DISABLED — correct target address for CGUnit_C::SetPosition has not
//         been identified. The previously used address 0x613E90 is actually
//         EjectPassengerFromSeat (Lua binding). Hooking it would intercept
//         vehicle eject commands instead of position updates, causing both
//         incorrect smoothing behavior and crashes.
//
//         To implement this properly, one must:
//         1. Find CGMovementInfo::SetPosition in the binary (likely near the
//            movement packet handlers at ~0x7270xx-0x7280xx range).
//         2. Verify the calling convention (__thiscall with CGMovementInfo* this).
//         3. Verify the parameter layout (C3Vector* pos, float facing).
// ============================================================================

#include "movement_smoothing.h"
#include "MinHook.h"

extern "C" void Log(const char* fmt, ...);

namespace MovementSmoothing {

    bool Init() {
        Log("[MovementSmoothing] DISABLED (correct SetPosition address not identified; "
            "0x613E90 is EjectPassengerFromSeat, not SetPosition)");
        return false;
    }

    void Shutdown() {
        // Nothing to clean up — hook was never installed
    }

    void SmoothPosition(void* /*entity*/, float* /*x*/, float* /*y*/, float* /*z*/) {
        // Stub — not active
    }
}
