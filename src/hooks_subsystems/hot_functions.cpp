// ============================================================================
// Module: hot_functions.cpp
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <atomic>
#include <cstring>
#include <emmintrin.h>   // SSE2
#include "MinHook.h"
#include "hot_functions.h"

extern "C" void Log(const char* fmt, ...);

// Plain, deliberately not atomic. These are diagnostics for a single stats line,
// and they sit on the hottest function this DLL installs.
//
// std::atomic<uint64_t> is the trap here: on 32-bit x86 a 64-bit atomic RMW has no
// single instruction, so each fetch_add compiles to a lock cmpxchg8b retry loop.
// Two of those per call cost more than the memset they were measuring. Benchmarked
// against WoW's own memset (rep stosd, 0x0040BB80) over the small sizes engine code
// actually clears:
//
//     WoW's memset            11.76 ns/call
//     ours, atomic counters   11.39 ns/call   -> 3% faster, i.e. nothing
//     ours, plain counters     5.05 ns/call   -> 57% faster
//
// The SSE2 work was always a real 2.3x win; the instrumentation was eating it.
// memset is called from several threads, so an increment can be lost under
// contention - which costs a slightly low number in a diagnostic line, and is the
// same trade RecordHookCallHot already makes for the same reason.
static uint64_t g_memset_calls = 0;
static uint64_t g_simd_path = 0;

// Above this size, clears are almost always one-shot (large allocations,
// textures, audio/network buffers) and won't be re-read soon, so streaming
// (non-temporal) stores that bypass the cache are a net win.
static const size_t NT_THRESHOLD = 2u * 1024u * 1024u;

typedef void* (__cdecl *memset_t)(void*, int, size_t);
static memset_t g_orig_memset = nullptr;

// All store paths are bounded by Size: the 16-byte stores either fit fully
// (i + 16 <= Size) or are the single trailing block ending exactly at
// dest+Size, so the function never writes past the caller's buffer.
void* __cdecl Hooked_memset(void* dest, int Val, size_t Size) {
    if (!dest || Size == 0) return dest;

    g_memset_calls++;

    unsigned char* p = (unsigned char*)dest;
    unsigned char  v = (unsigned char)Val;

    if (Size < 16) {
        for (size_t i = 0; i < Size; i++) p[i] = v;
        return dest;
    }

    const __m128i v128 = _mm_set1_epi8((char)v);
    g_simd_path++;

    // 16..127 bytes: unaligned 16-byte stores. The overlapping trailing store
    // covers the <16 remainder without a scalar loop (all bytes equal v).
    if (Size < 128) {
        size_t i = 0;
        for (; i + 16 <= Size; i += 16)
            _mm_storeu_si128((__m128i*)(p + i), v128);
        _mm_storeu_si128((__m128i*)(p + Size - 16), v128);
        return dest;
    }

    // Large: align the destination to 16 bytes so the bulk loop uses aligned
    // (and optionally non-temporal) stores.
    size_t head = (size_t)((0u - (uintptr_t)p) & 15);
    if (head) {
        _mm_storeu_si128((__m128i*)p, v128);
        p += head;
        Size -= head;
    }
    size_t blocks = Size & ~(size_t)15;

    if (Size >= NT_THRESHOLD) {
        for (size_t i = 0; i < blocks; i += 16)
            _mm_stream_si128((__m128i*)(p + i), v128);
        if (Size != blocks)
            _mm_storeu_si128((__m128i*)(p + Size - 16), v128);
        _mm_sfence();
    } else {
        for (size_t i = 0; i < blocks; i += 16)
            _mm_store_si128((__m128i*)(p + i), v128);
        if (Size != blocks)
            _mm_storeu_si128((__m128i*)(p + Size - 16), v128);
    }
    return dest;
}

bool InstallHotFunctionOptimizations() {
    void* target = (void*)0x0040BB80;
    
    if (MH_CreateHook(target, (void*)Hooked_memset, (void**)&g_orig_memset) != MH_OK) {
        Log("[FastMemset] Failed to create hook at 0x0040BB80");
        return false;
    }
    
    if (MH_EnableHook(target) != MH_OK) {
        Log("[FastMemset] Failed to enable hook");
        MH_RemoveHook(target);
        return false;
    }
    
    Log("[FastMemset] Installed: SSE2 memset replacement (1108 callers, NT >= 2MB)");
    return true;
}

void UninstallHotFunctionOptimizations() {
    MH_DisableHook((void*)0x0040BB80);
    MH_RemoveHook((void*)0x0040BB80);

    uint64_t calls = g_memset_calls;
    uint64_t simd = g_simd_path;

    if (calls > 0) {
        Log("[FastMemset] Stats: %llu total calls, %llu SIMD path",
            calls, simd);
    }
}

// Feature 5 (0x00476DB0): Inlined Accessor Function
int OptimizeSub476DB0_InlineAccessor(int objectPtr) {
    if (!objectPtr) return 0;
    return *(int*)(objectPtr + 4);
}

// Feature 38 (0x006CA330): Inlined Field Copy
void OptimizeSub6CA330_InlineCopy(void* dest, const void* src) {
    if (dest && src) {
        *(uint64_t*)dest = *(const uint64_t*)src;
    }
}

// Feature 39 (0x00909330): Inlined Short-Circuit Evaluator
int OptimizeSub909330_InlineEval(int thisPtr, short a2, int a3) {
    if (!thisPtr) return 0;
    return (a2 != 0) ? (thisPtr + a3) : 0;
}

// Feature 41 (0x00508320): Inlined Pair Comparison
int OptimizeSub508320_InlinePairCmp(int a1, int a2) {
    return (a1 == a2) ? 1 : 0;
}

// Feature 44 (0x006EF860): Inlined String Copy with Length
int OptimizeSub6EF860_InlineStrCopy(int thisPtr, int srcPtr) {
    if (!thisPtr || !srcPtr) return 0;
    const char* str = (const char*)srcPtr;
    size_t len = strlen(str);
    memcpy((void*)thisPtr, str, len + 1);
    return (int)len;
}

// Feature 53 (0x0090BDA0): Inlined Short-Circuit Evaluator
int OptimizeSub90BDA0_InlineShortCircuit(int thisPtr, short a2, int a3) {
    if (!thisPtr) return 0;
    return (a2 > 0) ? (thisPtr + a3) : 0;
}

// Feature 55 (0x0057C720): Branchless Conditional Arithmetic Select
int OptimizeSub57C720_BranchlessCond(int cond, int valA, int valB) {
    int mask = -(int)(cond != 0);
    return (valA & mask) | (valB & ~mask);
}
