#include "network_guid_sse2.h"
#include <emmintrin.h>
#include "version.h"

static uint8_t g_popcntLut[256];
static bool g_guidTablesInit = false;

void InitNetworkGuidSSE2() {
    if (g_guidTablesInit) return;
    
    for (int i = 0; i < 256; ++i) {
        uint8_t count = 0;
        for (int b = 0; b < 8; ++b) {
            if (i & (1 << b)) count++;
        }
        g_popcntLut[i] = count;
    }
    g_guidTablesInit = true;
}

uint32_t FastUnpackGuidSSE2(const uint8_t* buffer, uint32_t remaining, uint64_t* out_guid) {
    if (remaining == 0) {
        *out_guid = 0;
        return 0;
    }
    
    uint8_t mask = buffer[0];
    if (mask == 0) {
        *out_guid = 0;
        return 1;
    }
    
    uint8_t count = g_popcntLut[mask];
    if (remaining < 1u + count) {
        *out_guid = 0;
        return 0; // Error, buffer underflow
    }
    
    // Fast unrolled branchless deposit.
    // Extremely fast on modern pipelines without relying on SSSE3 (pshufb) or BMI2 (pdep)
    uint64_t guid = 0;
    uint32_t pos = 1;
    
    if (mask & 1) guid |= ((uint64_t)buffer[pos++]) << 0;
    if (mask & 2) guid |= ((uint64_t)buffer[pos++]) << 8;
    if (mask & 4) guid |= ((uint64_t)buffer[pos++]) << 16;
    if (mask & 8) guid |= ((uint64_t)buffer[pos++]) << 24;
    if (mask & 16) guid |= ((uint64_t)buffer[pos++]) << 32;
    if (mask & 32) guid |= ((uint64_t)buffer[pos++]) << 40;
    if (mask & 64) guid |= ((uint64_t)buffer[pos++]) << 48;
    if (mask & 128) guid |= ((uint64_t)buffer[pos++]) << 56;
    
    *out_guid = guid;
    return 1 + count;
}

bool InstallNetworkGuidSSE2Hooks() {
    InitNetworkGuidSSE2();
    // Offset discovery pending for CDataStore::GetPackedGuid.
    // Once found, install hook here using WineSafe_CreateHook.
    return true;
}
