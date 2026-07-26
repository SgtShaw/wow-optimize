// ============================================================================
// Module: frame_bench.cpp
// Description: Frame-time distribution benchmark.
// Safety & Threading: Main thread only (called from the present path).
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <cstring>

#include "frame_bench.h"
#include "core/config.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

// Frames presented during a zone load are not gameplay frames: one cold load can
// contribute several seconds of frame time and would own the entire tail.
namespace LuaOpt { bool IsLoadingMode(); }

namespace FrameBench {

// 0.1ms buckets up to 100ms covers everything that matters for smoothness; the
// rest lands in an overflow bucket and is still reflected in the max and in the
// "over 100ms" counter, so nothing is silently dropped.
static constexpr int    BUCKET_COUNT = 1000;
static constexpr double BUCKET_MS    = 0.1;

static uint32_t  g_buckets[BUCKET_COUNT];
static uint32_t  g_overflow    = 0;
static uint64_t  g_frames      = 0;
static double    g_sumMs       = 0.0;
static double    g_maxMs       = 0.0;
static double    g_over33      = 0.0;   // counters kept as double to avoid casts
static double    g_over50      = 0.0;
static double    g_over100     = 0.0;

static LARGE_INTEGER g_freq  = {};
static LARGE_INTEGER g_last  = {};
static Source        g_source = Source::None;
static bool          g_ready  = false;

static const char* SourceName(Source s) {
    switch (s) {
        case Source::D3D9Present: return "D3D9 Present";
        case Source::SwapHook:    return "OpenGL swap path";
        default:               return "none";
    }
}

// Identifies the configuration a log was produced under, so two runs can be
// confirmed to differ only where intended.
static uint32_t ConfigFingerprint() {
    const unsigned char* p = (const unsigned char*)&Config::g_settings;
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < sizeof(Config::Settings); i++) {
        h ^= p[i];
        h *= 0x01000193u;
    }
    return h;
}

void Init() {
    memset(g_buckets, 0, sizeof(g_buckets));
    g_overflow = 0;
    g_frames   = 0;
    g_sumMs    = 0.0;
    g_maxMs    = 0.0;
    g_over33 = g_over50 = g_over100 = 0.0;
    g_last.QuadPart = 0;
    g_source = Source::None;
    g_ready = QueryPerformanceFrequency(&g_freq) && g_freq.QuadPart > 0;
}

// Accumulation is kept separate from the clock so the distribution can be
// exercised on known input: given a synthetic series of frame times, the
// percentiles it reports are checkable without running the game.
static void Accumulate(double ms) {
    if (ms <= 0.0) return;

    g_frames++;
    g_sumMs += ms;
    if (ms > g_maxMs) g_maxMs = ms;
    if (ms > 33.0)  g_over33  += 1.0;
    if (ms > 50.0)  g_over50  += 1.0;
    if (ms > 100.0) g_over100 += 1.0;

    int b = (int)(ms / BUCKET_MS);
    if (b >= BUCKET_COUNT) g_overflow++;
    else                   g_buckets[b]++;
}

void OnPresent(Source src) {
    if (!g_ready) return;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    LONGLONG prev = g_last.QuadPart;
    g_last = now;
    g_source = src;

    // First frame after start has no meaningful predecessor. A frame that spans a
    // loading screen is discarded for the same reason: the gap is load time, not
    // frame time, and one cold zone load would own the whole tail.
    if (prev == 0 || LuaOpt::IsLoadingMode()) return;

    Accumulate((double)(now.QuadPart - prev) * 1000.0 / (double)g_freq.QuadPart);
}

// Walks the histogram once, filling in every requested percentile in order.
static void ComputePercentiles(const double* wanted, double* out, int n) {
    uint64_t seen = 0;
    int next = 0;
    for (int i = 0; i < BUCKET_COUNT && next < n; i++) {
        seen += g_buckets[i];
        while (next < n && (double)seen >= wanted[next] * (double)g_frames) {
            out[next] = (i + 1) * BUCKET_MS;
            next++;
        }
    }
    // Anything not reached inside the histogram lives in the overflow tail.
    while (next < n) {
        out[next] = (g_maxMs > BUCKET_COUNT * BUCKET_MS) ? g_maxMs
                                                        : BUCKET_COUNT * BUCKET_MS;
        next++;
    }
}

void Report(const char* reason) {
    // Saying nothing when no frames arrived is how the first version of this hid
    // its own failure: it was fed from the client's OpenGL swap path, which a D3D9
    // client never reaches, so a log with the hook reporting ACTIVE simply had no
    // FrameBench lines at all - indistinguishable from the feature not existing.
    // An instrument that measures nothing has to say so.
    if (!g_ready) {
        Log("[FrameBench] no timer available - QueryPerformanceFrequency failed");
        return;
    }
    if (g_frames == 0) {
        Log("[FrameBench] no frames recorded (%s) - the present hook never fired",
            reason ? reason : "report");
        return;
    }

    static const double wanted[] = { 0.50, 0.95, 0.99, 0.999 };
    double pct[4] = {};
    ComputePercentiles(wanted, pct, 4);

    double avg = g_sumMs / (double)g_frames;
    double seconds = g_sumMs / 1000.0;

    Log("[FrameBench] === FRAME TIME (%s) ===", reason ? reason : "report");
    Log("[FrameBench]   %llu frames over %.1fs, source: %s, config %08X, build %s",
        (unsigned long long)g_frames, seconds, SourceName(g_source),
        ConfigFingerprint(), WOW_OPTIMIZE_VERSION_STR);
    Log("[FrameBench]   avg %.2f ms (%.1f fps)   p50 %.2f   p95 %.2f   p99 %.2f   p99.9 %.2f   max %.2f",
        avg, avg > 0.0 ? 1000.0 / avg : 0.0, pct[0], pct[1], pct[2], pct[3], g_maxMs);
    Log("[FrameBench]   janky frames: >33ms %.0f (%.2f%%)  >50ms %.0f (%.2f%%)  >100ms %.0f (%.2f%%)",
        g_over33,  100.0 * g_over33  / (double)g_frames,
        g_over50,  100.0 * g_over50  / (double)g_frames,
        g_over100, 100.0 * g_over100 / (double)g_frames);
}

} // namespace FrameBench
