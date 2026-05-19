#pragma once

// Routes WriteFile() calls targeting WTF\...\SavedVariables\*.lua onto
// a worker thread. Handles tagged lazily on first WriteFile via
// GetFinalPathNameByHandle (CreateFile is already hooked elsewhere).

#include <windows.h>
#include <cstdint>

namespace SavedVarsAsync {

bool Init();
void Shutdown();
void Flush();

struct Stats {
    bool     active;
    uint32_t taggedHandles;
    uint64_t writesAsync;
    uint64_t writesSync;
    uint64_t bytesAsync;
    uint64_t writeFailures;
    uint32_t queueDepth;
};
void GetStats(Stats* out);

} // namespace SavedVarsAsync
