#pragma once

// Worker-pool decompressor for MPQ block reads. Pairs with the
// existing ReadFile MPQ cache in dllmain.cpp: when a multi-block
// read is detected, blocks are dispatched to workers for parallel
// inflate. Stub-safe - pool runs even if no offload arrives.

#include <windows.h>
#include <cstdint>

namespace MPQDecompressMT {

bool Init();
void Shutdown();
void OnFrame();

// Submit a compressed block for async inflate. Returns false if
// the queue is full (caller falls back to synchronous decompress).
bool SubmitBlock(const uint8_t* compressed, uint32_t compSize,
                 uint8_t* dst, uint32_t dstCapacity, HANDLE doneEvent);

struct Stats {
    bool     active;
    uint32_t workers;
    uint32_t queueDepth;
    uint64_t blocksProcessed;
    uint64_t bytesIn;
    uint64_t bytesOut;
    uint64_t fallbacks;
};
void GetStats(Stats* out);

} // namespace MPQDecompressMT
