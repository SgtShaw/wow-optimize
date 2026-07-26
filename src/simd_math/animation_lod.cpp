// ============================================================================
// Module: animation_lod.cpp
// Description: Animation LOD - throttle skeletal bone updates for background
//              models in crowded scenes (raids, cities). The "reduce work"
//              optimization, not "compute faster".
// Safety & Threading: the client calls UpdateBones from more than one thread, so
//              throttling decisions are taken on the main thread only and every
//              other thread passes straight through. Only ever SKIPS work (never
//              mutates engine state), and skips the way the engine already does.
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
// models update on a round-robin subset of ticks instead of every tick.
//
// A throttled tick MUST look like "this model is already current", not like "the
// update did not happen". Callers verify completion by re-reading the tick after
// the call - sub_830DC0 does it three times:
//
//     result = this[10];                              // anim context
//     if (this[15] != *(result + 20)) {               // lastTick != curTick?
//         ... sub_82F0F0(this, ...) ...               // update bones
//         v4 = this[10];
//         if (this[15] != *(v4 + 20))                 // did the update take?
//             ... entirely different fallback transform path ...
//     }
//
// An earlier version returned this+16 and left this+60 alone. That produces a
// state the engine itself never produces - tick not advanced AND no work done -
// so every caller concluded the update had failed and pushed the model down its
// fallback transform branch. The model snapped to the fallback placement on
// throttled ticks and back on updated ones: visible flickering at exactly the
// throttle rate (GitHub issue #46).
//
// So on a throttled tick we publish completion exactly the way the work path
// does - set this+60 to the current tick and return the animation context from
// this+40 - and simply leave the bone matrices at their previous values. That is
// the "already updated this tick" state every caller is built to handle, and the
// only difference from a real update is that the pose is one interval old, which
// is the entire point of the LOD. Everything is under SEH; any surprise falls
// back to the original function.
//
// Two refinements over a flat "half-rate when crowded" throttle, both using only
// data that is confirmed-available at this hook (no world position - the model
// animation object here has no camera distance, verified against UpdateBones and
// its callers, so we deliberately do NOT try to distance-gate and instead stay
// conservative to protect near models):
//   1. Crowd-graduated interval - the denser the scene, the longer the throttle
//      interval (2 -> 3 -> 4). The worst scenes, where CPU cost and FPS pain are
//      greatest, get the most relief; modest crowds get the gentlest throttle.
//      Capped at 4 (~15fps anim) precisely because we can't tell near from far
//      here, so we never throttle any model harder than stays imperceptible.
//   2. Per-model phase stagger - each model gets a stable phase offset from its
//      pointer hash, so the throttled models don't all skip the same frames.
//      The updates spread evenly across the interval instead of arriving in a
//      synchronized wave, which is what makes a 1/3 or 1/4 rate look smooth
//      rather than hitchy.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "core/config.h"
#include "animation_lod.h"
#include "diagnostics/crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

// Set by dllmain once the client's main thread is known.
extern DWORD g_mainThreadId;

namespace AnimationLod {

typedef int (__thiscall* UpdateBones_fn)(void* pThis, float* a2, int a3, float a4, float a5, float a6);
static UpdateBones_fn orig_UpdateBones = nullptr;
static bool g_installed = false;

// ---- tunables (safe conservative defaults; can be exposed later) -----------
// Only throttle when at least this many models were updated in the previous
// frame - i.e. only when the scene is actually crowded and CPU-bound. Light
// scenes run at full animation rate (zero quality cost when it doesn't matter).
static uint32_t g_crowdThreshold = 30;

// Crowd-graduated throttle interval: the busier the scene, the more we throttle.
// A model updates once every N ticks (2 = ~30fps anim, 3 = ~20fps, 4 = ~15fps).
// Capped at 4 on purpose: without per-model distance we can't protect a model
// that happens to be right in front of the camera inside a huge crowd, so we
// never push any model below ~15fps skeletal animation (imperceptible for
// background models, still tolerable for a near one).
static inline uint32_t IntervalForCrowd(uint32_t crowd) {
    if (crowd >= 160) return 4;   // very packed city / large BG
    if (crowd >= 80)  return 3;   // raid / busy city
    return 2;                     // modest crowd (>= threshold)
}

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
//
// The animation tick cannot be used on its own as a frame boundary. The client
// runs several animation contexts at once - the world has one, UI model frames
// (portraits, dressing room, model previews) keep their own - so consecutive
// models arriving at this hook can report completely unrelated tick values. Any
// change was treated as a new frame, so the counter was rolled several times per
// real frame: one roll captured the whole crowd, the next captured the one or two
// models belonging to the other context.
//
// The result was a throttle that flipped on and off about ten times a second -
// 137 models, then 1, then 137 - which is exactly the ~10Hz model flickering
// reported in issue #46. The fix is to roll only on the context we picked as our
// reference, and to require a real drop in crowd size before disengaging.
static volatile long g_callsThisFrame = 0;
static uint32_t      g_callsLastFrame = 0;
static uint32_t      g_lastFrameTick  = 0;
static uintptr_t     g_frameCtx       = 0;   // context whose tick marks our frames
static DWORD         g_lastRollTick   = 0;
static bool          g_crowded        = false;

// Re-latch the reference context if it has gone quiet - it may belong to a UI
// model frame the player just closed, and without this the counter would never
// roll again.
static const DWORD CTX_RELATCH_MS = 500;

// Engage and disengage at different crowd sizes. Without a gap, a scene sitting
// on the threshold toggles the throttle every frame, and the visible cost of
// toggling is far worse than either state on its own.
static const uint32_t CROWD_DISENGAGE = 22;   // vs g_crowdThreshold (30) to engage

// ---- stats -----------------------------------------------------------------
static uint64_t g_throttledTotal = 0;
static uint64_t g_updatedTotal   = 0;

static inline bool ValidPtr(uintptr_t p) { return p >= 0x10000 && p < 0xFFE00000; }

// The client updates bones from more than one thread. That was not in the design:
// every piece of crowd state below - the counter, the reference tick, the engaged
// flag - is shared and unsynchronized, so two threads roll the counter in turn and
// each sees what the other just reset. A tester log shows the result directly:
//
//     [TID: 15388] Throttle ENGAGED: 32 models in frame, interval 2
//     [TID: 15828] Throttle DISENGAGED: 0 models in frame
//     [TID: 15388] Throttle ENGAGED: 32 models in frame, interval 2
//
// A count of zero is not a scene that emptied, it is the other thread's reset. The
// throttle flips on and off several times a second, and every flip changes the
// animation rate of every model at once - which is what players see as flickering,
// and why weapons and freshly-killed corpses blink out.
//
// Throttling decisions are therefore main-thread-only. Models updated from any
// other thread always take the original path: they are counted by nobody and
// throttled by nobody, which is correct and, more importantly, deterministic.

static int __fastcall Hooked_UpdateBones(void* pThis, void* /*edx*/,
                                         float* a2, int a3, float a4, float a5, float a6)
{
    if (!ValidPtr((uintptr_t)pThis) || !orig_UpdateBones) return 0;

    if (g_mainThreadId != 0 && GetCurrentThreadId() != g_mainThreadId)
        return orig_UpdateBones(pThis, a2, a3, a4, a5, a6);

    __try {
        uintptr_t self = (uintptr_t)pThis;

        // Must be an active model (flags bit 0)
        int flags = *(int*)(self + 16);
        if ((flags & 1) == 0)
            return orig_UpdateBones(pThis, a2, a3, a4, a5, a6);

        // Safety Guard 0: bit 0x400 is cleared by the engine at the very end of a
        // completed update, so it is a per-update request the engine expects to be
        // consumed. Never throttle a model that still has it set.
        if ((flags & 0x400) != 0)
            return orig_UpdateBones(pThis, a2, a3, a4, a5, a6);

        // Safety Guard 1: Never throttle attachment models (e.g. weapons, shoulders, helms)
        // Attachment models have parent model pointers or attachment flags set at offset +0x24 / +0x28
        uintptr_t parentModel = *(uintptr_t*)(self + 0x24);
        if (ValidPtr(parentModel)) {
            g_updatedTotal++;
            return orig_UpdateBones(pThis, a2, a3, a4, a5, a6);
        }

        // Safety Guard 2: Never throttle local player character model
        uintptr_t playerUnit = *(uintptr_t*)0x00BD07A0;
        if (ValidPtr(playerUnit)) {
            uintptr_t playerModel = *(uintptr_t*)(playerUnit + 0x118);
            if (self == playerModel) {
                g_updatedTotal++;
                return orig_UpdateBones(pThis, a2, a3, a4, a5, a6);
            }
        }

        // Safety Guard 3: Never throttle target model
        uintptr_t targetUnit = *(uintptr_t*)0x00BD07B0;
        if (ValidPtr(targetUnit)) {
            uintptr_t targetModel = *(uintptr_t*)(targetUnit + 0x118);
            if (self == targetModel) {
                g_updatedTotal++;
                return orig_UpdateBones(pThis, a2, a3, a4, a5, a6);
            }
        }

        uintptr_t animCtx = *(uintptr_t*)(self + 40);
        if (!ValidPtr(animCtx))
            return orig_UpdateBones(pThis, a2, a3, a4, a5, a6);

        uint32_t tick = *(uint32_t*)(animCtx + 20);   // current animation tick
        uint32_t last = *(uint32_t*)(self + 60);      // tick this model last updated

        // A model that has never completed an update must never be throttled. The
        // work path is what first fills in derived state the caller relies on -
        // notably the bounding magnitude at +136, which is what the scene culls
        // against; leaving it at zero makes the model cull away entirely.
        //
        // This is reachable because the round-robin table is keyed by the model
        // pointer, and the client frees and reuses those. A mob dies, its model is
        // released, a new model lands on the same address, and our stale slot says
        // "this one updated recently" - so the new model is throttled before it has
        // ever been computed, and stays invisible until something forces an update.
        // That is the corpse that only reappears once you click its loot sparkle.
        if (last == 0) {
            g_updatedTotal++;
            return orig_UpdateBones(pThis, a2, a3, a4, a5, a6);
        }

        // Frame boundary: only the reference context's tick marks a frame.
        DWORD nowMs = GetTickCount();
        if (g_frameCtx == 0 || (DWORD)(nowMs - g_lastRollTick) > CTX_RELATCH_MS) {
            g_frameCtx = animCtx;
            g_lastRollTick = nowMs;
        }

        if (animCtx == g_frameCtx && tick != g_lastFrameTick) {
            g_callsLastFrame = (uint32_t)InterlockedExchange(&g_callsThisFrame, 0);
            g_lastRollTick = nowMs;

            // Hysteresis: engage at the threshold, disengage only once the scene
            // has genuinely thinned out.
            bool crowded = g_crowded ? (g_callsLastFrame >= CROWD_DISENGAGE)
                                     : (g_callsLastFrame >= g_crowdThreshold);

            // Record every engage/disengage. Without this a log only shows the
            // running totals, so a report of a visual artifact cannot be matched
            // against whether the throttle was even running at the time - which
            // is exactly the ambiguity that made issue #46 hard to pin down.
            if (crowded != g_crowded) {
                if (crowded) {
                    CrashDumper::Trace("ANIMLOD engage: %u models, interval %u",
                                       g_callsLastFrame, IntervalForCrowd(g_callsLastFrame));
                    Log("[AnimationLod] Throttle ENGAGED: %u models in frame, interval %u",
                        g_callsLastFrame, IntervalForCrowd(g_callsLastFrame));
                } else {
                    CrashDumper::Trace("ANIMLOD disengage: %u models", g_callsLastFrame);
                    Log("[AnimationLod] Throttle DISENGAGED: %u models in frame", g_callsLastFrame);
                }
                g_crowded = crowded;
            }
            g_lastFrameTick = tick;
        }
        InterlockedIncrement(&g_callsThisFrame);

        // Engine already skipped it this tick, or scene is light -> full rate
        if (tick == last || !g_crowded) {
            g_updatedTotal++;
            return orig_UpdateBones(pThis, a2, a3, a4, a5, a6);
        }

        // Throttle distant background models in crowded scenes
        uint32_t interval = IntervalForCrowd(g_callsLastFrame);
        uint32_t h = HashPtr(pThis);
        Slot& s = g_slots[h];

        if (s.key != pThis) {
            uint32_t phase = HashPtr((void*)~(uintptr_t)pThis) % (interval ? interval : 1);
            s.key = pThis;
            s.tick = tick - phase;
            g_updatedTotal++;
            return orig_UpdateBones(pThis, a2, a3, a4, a5, a6);
        }

        if ((uint32_t)(tick - s.tick) >= interval) {
            s.tick = tick;
            g_updatedTotal++;
            return orig_UpdateBones(pThis, a2, a3, a4, a5, a6);
        }

        // Throttle: publish completion the same way the work path does, so callers
        // that re-check the tick see "already current" instead of "update failed"
        // and keep using the bone matrices already in this+152.
        *(uint32_t*)(self + 60) = tick;
        g_throttledTotal++;
        return (int)animCtx;
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
    Log("[AnimationLod] ACTIVE: crowd-graduated bone-update throttle (engage>=%u models, "
        "interval 2/3/4 by crowd size, per-model phase-staggered). Background skeletal "
        "animation drops to ~30/20/15fps as scenes get denser.", g_crowdThreshold);
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
