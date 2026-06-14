// ================================================================
// SSE2 memcpy replacement for the 16-255 byte non-overlapping range
// ================================================================
// WoW's _memcpy (0x40CB10, 719 xrefs) handles overlap (memmove
// semantics) and uses __VEC_memcpy for >= 256B when SSE2 is available.
// Below 256B it falls through to dword-scalar (rep movsd).
//
// This hook intercepts only the non-overlapping 16-255B range with
// SSE2 unaligned loads/stores (~4x faster than dword-scalar).
// All other paths (overlap, >= 256B, < 16B) fall through to original
// to preserve memmove semantics and the VEC fast path.
// ================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
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

// Above this size a non-overlapping copy is almost always one-shot bulk data
// (textures, model/sound buffers, decompressed MPQ blocks). WoW's VEC memcpy
// uses plain movdqa there; streaming (non-temporal) stores avoid evicting the
// working set, which matters most during loading screens.
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
    if (!dest || !src || Size == 0) return g_orig_memcpy(dest, src, Size);

    g_total_calls++;

    const unsigned char* d = (const unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;

    // Overlap → fall through to original (preserves memmove semantics)
    if (ranges_overlap_up(d, s, Size) || ranges_overlap_down(d, s, Size)) {
        g_fallback_path++;
        return g_orig_memcpy(dest, src, Size);
    }

    // < 16B → original dword-scalar path is fine
    if (Size < 16) {
        g_fallback_path++;
        return g_orig_memcpy(dest, src, Size);
    }

    // Very large non-overlapping copy: stream with non-temporal stores so the
    // bulk data does not pollute the cache (WoW's VEC path does not do this).
    if (Size >= NT_THRESHOLD) {
        g_nt_path++;
        unsigned char* pd = (unsigned char*)dest;
        const unsigned char* ps = (const unsigned char*)src;

        // Align the destination so movntdq (which requires 16-byte alignment)
        // is legal; the source stays unaligned (loadu).
        size_t head = (0u - (uintptr_t)pd) & 15;
        if (head) {
            _mm_storeu_si128((__m128i*)pd, _mm_loadu_si128((const __m128i*)ps));
            pd += head; ps += head; Size -= head;
        }
        size_t blocks = Size & ~(size_t)15;
        for (size_t i = 0; i < blocks; i += 16) {
            // Prefetch the source ahead with a non-temporal hint to hide memory
            // latency. _mm_prefetch never faults (a hint, dropped for invalid
            // addresses), so reading past the buffer end here is harmless.
            _mm_prefetch((const char*)(ps + i + 512), _MM_HINT_NTA);
            __m128i v = _mm_loadu_si128((const __m128i*)(ps + i));
            _mm_stream_si128((__m128i*)(pd + i), v);
        }
        if (Size != blocks) {
            _mm_storeu_si128((__m128i*)(pd + Size - 16),
                             _mm_loadu_si128((const __m128i*)(ps + Size - 16)));
        }
        _mm_sfence();
        return dest;
    }

    // 256B .. NT_THRESHOLD → let original handle (VEC/SSE2 path already fast)
    if (Size >= 256) {
        g_fallback_path++;
        return g_orig_memcpy(dest, src, Size);
    }

    // 16-255B non-overlapping: SSE2 unaligned copy
    g_sse2_path++;

    unsigned char* pd = (unsigned char*)dest;
    const unsigned char* ps = (const unsigned char*)src;

    if (Size < 128) {
        // Unaligned 16-byte blocks + overlapping trailing store
        size_t i = 0;
        for (; i + 16 <= Size; i += 16) {
            __m128i v = _mm_loadu_si128((const __m128i*)(ps + i));
            _mm_storeu_si128((__m128i*)(pd + i), v);
        }
        // Single overlapping trailing store covers the <16 remainder
        _mm_storeu_si128((__m128i*)(pd + Size - 16),
                         _mm_loadu_si128((const __m128i*)(ps + Size - 16)));
        return dest;
    }

    // 128-255B: align the destination for the bulk loop
    size_t head = (0u - (uintptr_t)pd) & 15;
    if (head) {
        __m128i v = _mm_loadu_si128((const __m128i*)ps);
        _mm_storeu_si128((__m128i*)pd, v);
        pd += head;
        ps += head;
        Size -= head;
    }

    size_t blocks = Size & ~(size_t)15;
    for (size_t i = 0; i < blocks; i += 16) {
        __m128i v = _mm_loadu_si128((const __m128i*)(ps + i));
        _mm_store_si128((__m128i*)(pd + i), v);
    }
    if (Size != blocks) {
        _mm_storeu_si128((__m128i*)(pd + Size - 16),
                         _mm_loadu_si128((const __m128i*)(ps + Size - 16)));
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
