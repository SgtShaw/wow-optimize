#pragma once
#include <windows.h>

namespace M2MatrixSimd {
    bool Init();
    void Shutdown();
    void TransformVertexSse(const float* matrix4x4, const float* inPos, float* outPos);
}
