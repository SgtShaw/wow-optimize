#pragma once

// Worker pool reserved for parallel M2 bone-matrix interpolation.
// Pool spins up at Init() and stays idle until SubmitBones() is
// invoked from a verified animation hook (pending IDA pass).

#include <windows.h>
#include <cstdint>

namespace AnimMT {

bool Init();
void Shutdown();
void OnFrame();

bool SubmitBones(void* m2Instance, uint32_t boneCount, HANDLE doneEvent);

struct Stats {
    bool     active;
    uint32_t workers;
    uint32_t queueDepth;
    uint64_t batches;
    uint64_t fallbacks;
};
void GetStats(Stats* out);

} // namespace AnimMT
