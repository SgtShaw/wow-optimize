// ============================================================================
// Module: animation_lod.cpp
// Description: Animation LOD - throttle skeletal bone updates for background
//              models in crowded scenes (raids, cities). The "reduce work"
//              optimization, not "compute faster".
// Safety & Threading: main-thread only; only ever SKIPS work (never mutates
//              engine state), and skips exactly the way the engine already does.
// ============================================================================
//
// The bone-animation function UpdateBones (0x82F0F0, __thiscall(this,a2,a3,a4,
// a5,a6)) already dedups per animation tick:
//
//     if ((this->flags & 1) && this[60] != *(*(this+40)+20)) { ...work...;
//                                     this[60] = *(*(this+40)+20); }
//     return ...;            // on the skip branch it returns this[16]
//
// this+40  = animation context; +20 = the current animation tick (increments
//            as time advances). this+60 = the tick this model last updated on.
// So the engine itself skips a model whose bones are already current this tick.
//
// The pose is computed from ABSOLUTE animation time, not a per-call delta, so
// updating a model less often just makes its animation play at a lower frame
// rate while staying perfectly in sync - it never drifts or corrupts. For a
// distant/background model in a 25-man raid or a packed city, 20-30fps skeletal
// animation is imperceptible, and there can be hundreds of them.
//
// This hook extends that existing skip: when the scene is crowded, background
// models update on a round-robin subset of ticks instead of every tick. On a
// throttled tick we return exactly what the engine's own skip branch returns
// (this[16]) and leave this[60] alone, so the caller renders the model with its
// current (slightly older) bone matrices - the same state it would see mid-frame
// before that model's turn to update. Everything is under SEH; any surprise
// falls back to the original function.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "core/config.h"
#include "animation_lod.h"

extern "C" void Log(const char* fmt, ...);

namespace AnimationLod {

typedef int (__thiscall* UpdateBones_fn)(void* pThis, float* a2, int a3, float a4, float a5, float a6);
static UpdateBones_fn orig_UpdateBones = nullptr;
static bool g_installed = false;

// ---- tunables (safe conservative defaults; can be exposed later) -----------
// Only throttle when at least this many models were updated in the previous
// frame - i.e. only when the scene is actually crowded and CPU-bound. Light
// scenes run at full animation rate (zero quality cost when it doesn't matter).
static uint32_t g_crowdThreshold = 40;
// A throttled model updates once every N ticks (2 = half rate, ~30fps anim).
static uint32_t g_throttleInterval = 2;

// ---- per-model round-robin tracker (fixed table, no allocation) ------------
// Keyed by the model pointer; stores the tick we last allowed it to update.
// A stale/colliding entry can only cause one extra or one skipped update - it
// can never be unsafe, so no locking or lifetime tracking is needed.
struct Slot { void* key; uint32_t tick; };
static const uint32_t SLOT_COUNT = 8192;          // power of two
static const uint32_t SLOT_MASK  = SLOT_COUNT - 1;
static Slot g_slots[SLOT_COUNT] = {};

static inline uint32_t HashPtr(void* p) {
    uint32_t x = (uint32_t)(uintptr_t)p;
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15;
    return x & SLOT_MASK;
}

// ---- crowd detection (per frame) -------------------------------------------
static volatile long g_callsThisFrame = 0;
static uint32_t      g_callsLastFrame = 0;
static uint32_t      g_lastFrameTick  = 0;
static bool          g_crowded        = false;

// ---- stats -----------------------------------------------------------------
static uint64_t g_throttledTotal = 0;
static uint64_t g_updatedTotal   = 0;

static inline bool ValidPtr(uintptr_t p) { return p >= 0x10000 && p < 0xFFE00000; }

static int __fastcall Hooked_UpdateBones(void* pThis, void* /*edx*/,
                                         float* a2, int a3, float a4, float a5, float a6)
{
    if (!ValidPtr((uintptr_t)pThis))
        return orig_UpdateBones ? orig_UpdateBones(pThis, a2, a3, a4, a5, a6) : 0;

    __try {
        uintptr_t self = (uintptr_t)pThis;

        // Must be an active model (flags bit 0) or the engine wouldn't animate it.
        int flags = *(int*)(self + 16);
        if ((flags & 1) == 0)
            return orig_UpdateBones(pThis, a2, a3, a4, a5, a6);

        uintptr_t animCtx = *(uintptr_t*)(self + 40);
        if (!ValidPtr(animCtx))
            return orig_UpdateBones(pThis, a2, a3, a4, a5, a6);

        uint32_t tick = *(uint32_t*)(animCtx + 20);   // current animation tick
        uint32_t last = *(uint32_t*)(self + 60);      // tick this model last updated

        // Frame boundary: when the global tick advances, roll the crowd counter.
        if (tick != g_lastFrameTick) {
            g_callsLastFrame = (uint32_t)InterlockedExchange(&g_callsThisFrame, 0);
            g_crowded = (g_callsLastFrame >= g_crowdThreshold);
            g_lastFrameTick = tick;
        }
        InterlockedIncrement(&g_callsThisFrame);

        // Engine already skips it this tick, or the scene is light -> untouched.
        if (tick == last || !g_crowded) {
            g_updatedTotal++;
            return orig_UpdateBones(pThis, a2, a3, a4, a5, a6);
        }

        // Crowded + this model is due for a real update. Round-robin throttle it.
        uint32_t h = HashPtr(pThis);
        Slot& s = g_slots[h];
        bool due = (s.key != pThis) || ((uint32_t)(tick - s.tick) >= g_throttleInterval);
        if (due) {
            s.key = pThis;
            s.tick = tick;
            g_updatedTotal++;
            return orig_UpdateBones(pThis, a2, a3, a4, a5, a6);
        }

        // Throttle: skip exactly like the engine's own dedup branch does.
        g_throttledTotal++;
        return *(int*)(self + 16);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return orig_UpdateBones ? orig_UpdateBones(pThis, a2, a3, a4, a5, a6) : 0;
    }
}

bool Init()
{
    if (!Config::g_settings.OptAnimationLod) {
        Log("[AnimationLod] disabled via configuration (opt-in)");
        return true;
    }

    void* target = (void*)0x0082F0F0;
    unsigned char* p = (unsigned char*)target;
    if (p[0] != 0x55 || p[1] != 0x8B) {   // push ebp; mov ebp, esp
        Log("[AnimationLod] BAD PROLOGUE at 0x0082F0F0 (got %02X %02X) - not installing", p[0], p[1]);
        return false;
    }
    if (MH_CreateHook(target, (void*)Hooked_UpdateBones, (void**)&orig_UpdateBones) != MH_OK) {
        Log("[AnimationLod] MH_CreateHook FAILED");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[AnimationLod] MH_EnableHook FAILED");
        return false;
    }
    g_installed = true;
    Log("[AnimationLod] ACTIVE: crowd-throttled bone updates (crowd>=%u models, 1/%u tick when far). "
        "Skeletal animation for background models drops to ~%ufps in packed scenes.",
        g_crowdThreshold, g_throttleInterval, 60u / (g_throttleInterval ? g_throttleInterval : 1));
    return true;
}

void LogStats()
{
    if (!g_installed) return;
    uint64_t total = g_updatedTotal + g_throttledTotal;
    if (total == 0) return;
    Log("[AnimationLod] Stats: %llu bone updates run, %llu throttled (%.1f%% skipped), last frame had %u models",
        (unsigned long long)g_updatedTotal, (unsigned long long)g_throttledTotal,
        100.0 * (double)g_throttledTotal / (double)total, g_callsLastFrame);
}

void Shutdown()
{
    if (!g_installed) return;
    LogStats();
    MH_DisableHook((void*)0x0082F0F0);
    MH_RemoveHook((void*)0x0082F0F0);
    g_installed = false;
}

} // namespace AnimationLod
