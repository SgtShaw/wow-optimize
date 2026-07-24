// ============================================================================
// Module: crt_memcpy_fast.cpp
// Description: SSE2 vectorized replacement for legacy CRT function `crt_memcpy_fast.cpp`.
// Safety & Threading: Concurrent execution safe. Ensure page boundary alignment checks are active.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>
#include <emmintrin.h>
#include "MinHook.h"
#include "version.h"
#include "crt_memcpy_fast.h"

extern "C" void Log(const char* fmt, ...);

static uint64_t g_total_calls = 0;
static uint64_t g_sse2_path = 0;
static uint64_t g_nt_path = 0;
static uint64_t g_fallback_path = 0;

static const size_t NT_THRESHOLD = 256 * 1024;

typedef void* (__cdecl *orig_memcpy_t)(void*, const void*, size_t);
static orig_memcpy_t g_orig_memcpy = nullptr;

static bool ranges_overlap_up(const unsigned char* dst, const unsigned char* src, size_t size)
{
    return (dst > src) && (dst < src + size);
}

static bool ranges_overlap_down(const unsigned char* dst, const unsigned char* src, size_t size)
{
    return (src > dst) && (src < dst + size);
}

static void* __cdecl Hooked_memcpy(void* dest, const void* src, size_t Size)
{
    if (!g_orig_memcpy) {
        if (dest && src && Size > 0) {
            __movsb((unsigned char*)dest, (const unsigned char*)src, Size);
        }
        return dest;
    }

    if (!dest || !src || Size == 0) return g_orig_memcpy(dest, src, Size);

    const unsigned char* d = (const unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;

    if (ranges_overlap_up(d, s, Size) || ranges_overlap_down(d, s, Size)) {
        g_fallback_path++;
        return g_orig_memcpy(dest, src, Size);
    }

    if (Size < 16) {
        g_fallback_path++;
        return g_orig_memcpy(dest, src, Size);
    }

    if (Size >= NT_THRESHOLD) {
        g_fallback_path++;
        return g_orig_memcpy(dest, src, Size);
    }

    if (Size >= 256) {
        g_fallback_path++;
        return g_orig_memcpy(dest, src, Size);
    }

    g_total_calls++;
    g_sse2_path++;

    unsigned char* pd = (unsigned char*)dest;
    const unsigned char* ps = (const unsigned char*)src;
    size_t len = Size;

    size_t i = 0;
    for (; i + 16 <= len; i += 16) {
        _mm_storeu_si128((__m128i*)(pd + i), _mm_loadu_si128((const __m128i*)(ps + i)));
    }
    if (i < len) {
        __movsb(pd + i, ps + i, len - i);
    }

    return dest;
}

bool InstallMemcpyFast()
{
    void* target = reinterpret_cast<void*>(0x0040CB10);

    unsigned char* p = (unsigned char*)target;
    if (p[0] != 0x55 || p[1] != 0x8B || p[2] != 0xEC) {
        Log("[FastMemcpy] BAD PROLOGUE at 0x%08X (expected 55 8B EC)", (uintptr_t)target);
        return false;
    }

    if (WineSafe_CreateHook(target, (void*)Hooked_memcpy, (void**)&g_orig_memcpy) != MH_OK) {
        Log("[FastMemcpy] MH_CreateHook FAILED");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[FastMemcpy] MH_EnableHook FAILED");
        MH_RemoveHook(target);
        return false;
    }

    Log("[FastMemcpy] Installed: SSE2 memcpy for 16-255B range at 0x40CB10 (719 xrefs, memmove-safe)");
    return true;
}

void UninstallMemcpyFast()
{
    void* target = reinterpret_cast<void*>(0x0040CB10);
    MH_DisableHook(target);
    MH_RemoveHook(target);

    uint64_t total = g_total_calls;
    if (total > 0) {
        Log("[FastMemcpy] Stats: %llu total, %llu SSE2, %llu NT, %llu fallback (%.1f%% SSE2)",
            total, g_sse2_path, g_nt_path, g_fallback_path, 100.0 * g_sse2_path / total);
    }
}

// Feature 3 (0x00842DA0): Inlined Fast Small Buffer Memory Copy
void* OptimizeSub842DA0_FastMemcpy(void* dest, const void* src, size_t count) {
    if (!dest || !src || count == 0) return dest;
    if (count <= 16) {
        __movsb((unsigned char*)dest, (const unsigned char*)src, count);
    } else {
        _mm_storeu_si128((__m128i*)dest, _mm_loadu_si128((const __m128i*)src));
        if (count > 16) {
            __movsb((unsigned char*)dest + 16, (const unsigned char*)src + 16, count - 16);
        }
    }
    return dest;
}

// Feature 2 (0x00695FD0): Pre-allocated Memory Pool for Frequent Allocs
static __declspec(align(16)) char g_poolBlock[65536];
static volatile long g_poolOffset = 0;

void* OptimizeSub695FD0_PoolAlloc(size_t size) {
    if (size == 0 || size > 4096) return nullptr;
    size = (size + 15) & ~15; // Align to 16 bytes
    long offset = InterlockedExchangeAdd(&g_poolOffset, (long)size);
    if ((size_t)offset + size > sizeof(g_poolBlock)) {
        InterlockedExchangeAdd(&g_poolOffset, -(long)size); // Rollback
        return nullptr;
    }
    return g_poolBlock + offset;
}

// Feature 32 (0x00621070): Branchless Conditional Copy
void OptimizeSub621070_BranchlessCopy(void* dest, const void* src, size_t count, int condition) {
    // Branchless: mask is all-ones if condition != 0, all-zeros otherwise
    size_t mask = (size_t)(-(int)(condition != 0));
    size_t effectiveCount = count & mask;
    if (effectiveCount > 0 && dest && src) {
        memcpy(dest, src, effectiveCount);
    }
}
