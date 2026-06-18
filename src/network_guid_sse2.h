#pragma once

#include <stdint.h>

// Initializes lookup tables for fast GUID unpacking
void InitNetworkGuidSSE2();

// Branchless packed GUID unpacker. Returns the number of bytes consumed (1 for mask + N data bytes).
// Does not advance read_pos; the caller must do that.
// Safe to call even at buffer boundaries.
uint32_t FastUnpackGuidSSE2(const uint8_t* buffer, uint32_t remaining, uint64_t* out_guid);

// Call this from dllmain.cpp during initialization
bool InstallNetworkGuidSSE2Hooks();
