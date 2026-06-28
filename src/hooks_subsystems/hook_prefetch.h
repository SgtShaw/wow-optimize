#pragma once

// ============================================================================
// Module: hook_prefetch.h
// Description: Supporting utility functions for `hook_prefetch.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `hook_prefetch.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Game Graphics, I/O and Subsystem Hooks
 * @architecture: Intercepts system APIs and resource loader loops to apply caching, coalescing and throttling.
 * @thread_affinity: Main Loop / Asynchronous Queue Execution
 * @regression_hazard: Invalid file handles or incorrect return value propagation will cause memory leaks or game client hangs.
 */


#ifndef HOOK_PREFETCH_H
#define HOOK_PREFETCH_H

#include <cstdint>
#include <cstddef>
#include <windows.h>

namespace HookPrefetch {
    bool InstallAll();
    void ShutdownAll();
    void DumpStats();
}

// F5: Memory prefetch for hot structures
void HookPrefetch_MemoryPrefetch(const void* addr, size_t len);

// F6: Lock-free counter read
LONG HookPrefetch_ReadCounter(volatile LONG* counter);

// F7: VTable entry cache
void* HookPrefetch_CacheVTableEntry(void* obj, uint32_t vtableOffset);

// F8: String compare short-circuit
int HookPrefetch_FastStrCmp(const char* a, size_t aLen, const char* b, size_t bLen);

// F9: Integer division by constant (compile-time macros)
#define HOOK_PREFETCH_DIV_BY_5(n)  ((int)((long long)(n) * 0xCCCCCCCDLL >> 34))
#define HOOK_PREFETCH_DIV_BY_10(n) ((int)((long long)(n) * 0xCCCCCCCCLL >> 35))
#define HOOK_PREFETCH_DIV_BY_100(n) ((int)((long long)(n) * 0x51EB851FLL >> 37))

// F10: SSE2 float batch operations
void HookPrefetch_BatchFloatAdd(float* dst, const float* src, int count);
void HookPrefetch_BatchFloatMul(float* dst, const float* src, int count);

// F11: Critical section spin try-enter
bool HookPrefetch_SpinTryEnter(CRITICAL_SECTION* cs, int maxSpins);

// F12: Heap size class pre-warm
void HookPrefetch_PrewarmSizeClasses();

// F13: Frame update coalescing
void HookPrefetch_BeginFrameCoalesce();

// F14: Atomic one-shot init flag
bool HookPrefetch_TryInit(int flagIndex);

// F15: Cache-line aligned allocation
void* HookPrefetch_AlignedAlloc(size_t size);
void HookPrefetch_AlignedFree(void* ptr);

// F16-F17: Fast memset/memcpy for small sizes
void HookPrefetch_FastMemset(void* dst, int val, size_t n);
void HookPrefetch_FastMemcpy(void* dst, const void* src, size_t n);

// F18: Branch prediction hints
#if defined(__GNUC__) || defined(__clang__)
#define HOOK_PREDICT_TRUE(x)  __builtin_expect(!!(x), 1)
#define HOOK_PREDICT_FALSE(x) __builtin_expect(!!(x), 0)
#else
#define HOOK_PREDICT_TRUE(x)  (x)
#define HOOK_PREDICT_FALSE(x) (x)
#endif

// F19: RDTSC-based microsecond timing
void HookPrefetch_InitTiming();
uint64_t HookPrefetch_RdtscDeltaUs(uint64_t startRdtsc);

// F20: Ring buffer stats collector
void HookPrefetch_RecordStat(const char* name, LONG value);

#endif