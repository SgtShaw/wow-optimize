#pragma once

// Worker pool for BLP pixel-format decode. SubmitDecode() returns
// false if the pool can't take work (queue full / disabled), and
// callers fall back to synchronous decode.

#include <windows.h>
#include <cstdint>

namespace TextureDecodeMT {

bool Init();
void Shutdown();
void OnFrame();

bool SubmitDecode(const uint8_t* blpData, uint32_t blpSize,
                  uint8_t* dstRGBA, uint32_t dstCapacity, HANDLE doneEvent);

struct Stats {
    bool     active;
    uint32_t workers;
    uint32_t queueDepth;
    uint64_t framesObserved;
    uint64_t fallbacks;
};
void GetStats(Stats* out);

} // namespace TextureDecodeMT
