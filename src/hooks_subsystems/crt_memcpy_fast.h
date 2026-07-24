#pragma once

// ============================================================================
// Module: crt_memcpy_fast.h
// Description: SSE2 vectorized replacement for legacy CRT function `crt_memcpy_fast.h`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================










bool InstallMemcpyFast();
void UninstallMemcpyFast();

// Feature 3 (0x00842DA0): Inlined Fast Small Buffer Memory Copy
void* OptimizeSub842DA0_FastMemcpy(void* dest, const void* src, size_t count);

// Feature 2 (0x00695FD0): Pre-allocated Memory Pool for Frequent Allocs
void* OptimizeSub695FD0_PoolAlloc(size_t size);

// Feature 32 (0x00621070): Branchless Conditional Copy
void OptimizeSub621070_BranchlessCopy(void* dest, const void* src, size_t count, int condition);
