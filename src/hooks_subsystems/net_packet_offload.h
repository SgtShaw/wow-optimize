#pragma once

#ifndef NET_PACKET_OFFLOAD_H
#define NET_PACKET_OFFLOAD_H

#include <cstdint>

namespace NetPacketOffload {

// Initialize network packet offloading detours and spawn helper threads
bool Init();

// Shut down hooks and join threads
void Shutdown();

// Feature 49 (0x006909A0): Branchless Opcode Dispatch Table
int OptimizeSub6909A0_BranchlessOpcode(uint16_t opcode, void* packetData);

} // namespace NetPacketOffload

#endif // NET_PACKET_OFFLOAD_H
