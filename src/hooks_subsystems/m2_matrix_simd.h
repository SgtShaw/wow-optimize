#pragma once
#include <windows.h>

namespace M2MatrixSimd {
    bool Init();
    void Shutdown();
    void TransformVertexSse(const float* matrix4x4, const float* inPos, float* outPos);

    // Feature 1 (0x006277F0): SoA Memory Access Optimization
    void OptimizeSub6277F0_SoA(const float* inAoS, float* outSoA, int count);
    // Feature 6 (0x008D67D0): Cache-Line Alignment for Mesh Bounds
    void OptimizeSub8D67D0_CacheAlign(const float* bounds, float* alignedOut);
    // Feature 7 (0x0083F190): Bone Matrix Packing
    void OptimizeSub83F190_BonePack(const float* srcMatrices, float* packedDest, int boneCount);
    // Feature 9 (0x009411A0): De-virtualized Transform Path
    void OptimizeSub9411A0_Devirtualize(void* transformObj, float* matrixOut);
    // Feature 20 (0x009B1B40): Vectorized 4x4 Matrix Multiply
    void OptimizeSub9B1B40_MatrixMul(float* outMat, const float* matA, const float* matB);
    // Feature 26 (0x0093E470): Lock-Free Matrix Update Slot
    bool OptimizeSub93E470_LockFree(volatile LONG* lockSlot, const float* newMatrix, float* targetMatrix);

    // De-virtualized Transform Path Fastpaths
    void OptimizeSub9A3F20_DevirtualizedEval(void* node, float* outMatrix);
    void OptimizeSub581E80_DevirtualizedScale(void* scaleObj, float* scaleVec);
}
