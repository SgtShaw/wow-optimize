#pragma once

// ============================================================================
// Module: frame_arena.h
// Description: Supporting utility functions for `frame_arena.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `frame_arena.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: High-Performance Memory Allocations
 * @architecture: Overrides the standard CRT memory management callbacks using mimalloc redirects.
 * @thread_affinity: Worker Thread / Concurrent Execution Safe
 * @regression_hazard: Mismatched allocations between CRT heaps and mimalloc will cause instant heap corruption.
 */



// 16 MB bump-allocator for sub-1KB wow.exe-originated mallocs. Resets
// on the main-thread Sleep boundary. User pointers carry a magic
// header so foreign frees (mimalloc / CRT) can range-test in O(1).

#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace FrameArena {

bool Init();
void Shutdown();

void* TryAlloc(size_t size);
bool  Owns(const void* p);
size_t SizeOf(const void* p);
void Reset();

// Stat hook for hooked_free / hooked_realloc when they detect arena
// pointers and skip the actual free.
void NoteNoOpFree();

struct Stats {
    bool     active;
    uint32_t capacity;
    uint32_t inUse;
    uint32_t resets;
    uint64_t totalAllocs;
    uint64_t totalAllocBytes;
    uint64_t fallbacks;
    uint64_t freesNoOp;
};
void GetStats(Stats* out);

} // namespace FrameArena
