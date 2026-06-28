#pragma once

// ============================================================================
// Module: network_guid_sse2.h
// Description: Supporting utility functions for `network_guid_sse2.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `network_guid_sse2.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Vectorized SIMD Math Operations
 * @architecture: Vectorizes legacy x87 FPU float operations using 128-bit SSE2 registers.
 * @thread_affinity: Main Loop / Worker Thread Safe
 * @regression_hazard: Unaligned vector load/store instructions or coordinate overlaps will cause memory violations or coordinate drift NaNs.
 */



#pragma region System Dependencies
#include <stdint.h>
#pragma endregion

#pragma region API Control Interfaces
/**
 * @domain: Network GUID Serialization
 * @rationale: Pre-calculates bit-counting structures to accelerate GUID parsing.
 */
void InitNetworkGuidSSE2();

/**
 * @domain: Network GUID Serialization
 * @rationale: Performs a branchless unpack of a packed network entity GUID.
 * @param buffer: Pointer to the source packed network stream.
 * @param remaining: The maximum readable bytes left in the buffer.
 * @param out_guid: Destination pointer for the unpacked 64-bit GUID.
 */
uint32_t FastUnpackGuidSSE2(const uint8_t* buffer, uint32_t remaining, uint64_t* out_guid);

/**
 * @domain: Network GUID Serialization
 * @rationale: Installs hooks on the game's internal GUID unpacking parser.
 */
bool InstallNetworkGuidSSE2Hooks();
#pragma endregion
