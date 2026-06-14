/*
 * memset replacement
 * Target: 0x0040BB80 (1108 callers)
 * void* __cdecl(void* dest, int Val, size_t Size)
 */

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

static std::atomic<uint64_t> g_memset_calls{0};
static std::atomic<uint64_t> g_simd_path{0};

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

    g_memset_calls.fetch_add(1, std::memory_order_relaxed);

    unsigned char* p = (unsigned char*)dest;
    unsigned char  v = (unsigned char)Val;

    if (Size < 16) {
        for (size_t i = 0; i < Size; i++) p[i] = v;
        return dest;
    }

    const __m128i v128 = _mm_set1_epi8((char)v);
    g_simd_path.fetch_add(1, std::memory_order_relaxed);

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

    uint64_t calls = g_memset_calls.load(std::memory_order_relaxed);
    uint64_t simd = g_simd_path.load(std::memory_order_relaxed);

    if (calls > 0) {
        Log("[FastMemset] Stats: %llu total calls, %llu SIMD path",
            calls, simd);
    }
}
