#pragma once

namespace M2BoneSimd {
    bool Init();
    void Shutdown();
    void VectorizedMatrixMultiply(float* outMatrix, const float* inMatrixA, const float* inMatrixB);
}
