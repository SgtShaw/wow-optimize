#pragma once

// ============================================================================
// Module: simd_math_fast.h
// Description: Hand-optimized SSE2 vector/matrix math helper fast paths.
// Safety & Threading: Thread-safe, executes on main/render threads.
// ============================================================================

namespace SimdMathFast {

bool Init();
void Shutdown();

} // namespace SimdMathFast
