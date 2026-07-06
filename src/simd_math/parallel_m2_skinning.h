#pragma once

// ============================================================================
// Module: parallel_m2_skinning.h
// Description: Parallel CPU-bound vertex skinning using worker threads.
// Safety & Threading: Thread-safe, executes across background worker threads.
// ============================================================================

namespace ParallelM2Skinning {

bool Init();
void Shutdown();

} // namespace ParallelM2Skinning
