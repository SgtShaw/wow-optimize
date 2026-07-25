#pragma once
#include <windows.h>

namespace VertexBufferPrealloc {
    bool Init();
    void Shutdown();
    void* AllocateBuffer(size_t size);
    void FreeBuffer(void* ptr);

    // Memory Pool & Lock-Free Slot Optimizations
    void* OptimizeSub4DAB40_PoolSlotAlloc(size_t size);
    void* OptimizeSub4C74F0_ObjPoolAlloc(size_t size);
    bool OptimizeSub927A40_LockFreeUpdate(volatile LONG* slot, const void* src, void* dst, size_t size);
}
