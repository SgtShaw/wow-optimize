#pragma once
#include <windows.h>

namespace PacketProcessingThrottle {
    bool Init();
    void Shutdown();
    bool ShouldThrottlePacket(unsigned int opcode);
}
