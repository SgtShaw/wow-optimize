// ============================================================================
// Module: nameplate_throttle.cpp
// Description: Nameplate LOD - skip the per-nameplate per-frame update for
//              non-target nameplates in crowded scenes (BGs, packed cities).
//              The "reduce work" optimization, not "compute faster".
// Safety & Threading: main-thread only; only ever SKIPS the update for a frame,
//              never mutates engine state. The skipped work is self-correcting.
// ============================================================================
//
// The per-nameplate update CNameplate::Update (0x0098E9F0, __thiscall(this,dt))
// runs once per nameplate per frame. It refreshes the target-priority layer, a
// highlight timer (this+752), the alpha, the threat/aggro border color, and the
// health-bar fill animation (timers this+760/764). Verified in IDA:
//
//   - It reads the current target GUID (qword_BD07B0) itself and the nameplate's
//     own GUID (this+680). Our hook re-checks that on EVERY call, so the target
//     nameplate is never throttled and target changes are never delayed.
//   - Screen POSITIONING (following the unit) is a SEPARATE function
//     (CNameplate::SetPosition 0x007256C0) that we do NOT touch. Skipping the
//     update therefore causes zero position lag - the plate still tracks its unit
//     every frame. Only cosmetic state (health-bar fill animation, threat color,
//     alpha) goes stale for the skipped frame.
//   - All its timers are dt-accumulated (this[752] -= dt*1000, this[760] -= dt).
//     So skipping a frame just doesn't advance them that frame - nothing breaks.
//
// When we DO let a throttled plate update, we pass the accumulated dt of the
// frames we skipped, so its timers/animations advance by the correct total time
// and play at the right speed - just at a lower update rate (like rendering that
// one plate at 30fps). This is exactly the Animation LOD philosophy applied to
// nameplates. Everything is under SEH; any surprise falls back to the original.
//
// Only engages when the scene actually has many nameplates (BG/city) and only
// for non-target plates - light scenes and your current target are untouched.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "core/config.h"
#include "nameplate_throttle.h"

extern "C" void Log(const char* fmt, ...);

namespace NameplateThrottle {

// __thiscall(this, float dt) expressed as __fastcall (edx is the unused dummy).
typedef void (__fastcall* Update_fn)(void* pThis, void* edx, float dt);
static Update_fn orig_Update = nullptr;
static bool g_installed = false;

// GetWorldMatrix __thiscall detour expressed as __fastcall
typedef void* (__fastcall* GetWorldMatrix_fn)(void* pThis, void* edx, void* a2);
static GetWorldMatrix_fn orig_GetWorldMatrix = nullptr;
static bool g_installed_matrix = false;

// ---- engine globals (verified in IDA) --------------------------------------
static volatile uint32_t* const g_frameCounter = (volatile uint32_t*)0x00CD87B0; // ++ per render frame
static const uint64_t*   const g_targetGuid    = (const uint64_t*)0x00BD07B0;     // current target GUID

// ---- tunables --------------------------------------------------------------
// Only throttle when at least this many nameplates were updated last frame -
// i.e. only in BGs/packed cities where it actually costs. Few plates = untouched.
static uint32_t g_crowdThreshold = 8;

// A throttled plate updates once every N frames. Graduated a little by how many
// plates are up. Kept conservative (max 3) - the only thing being throttled is
// cosmetic (health-bar animation etc.), positioning is untouched, so this is
// imperceptible for a non-target plate.
static inline uint32_t IntervalForCrowd(uint32_t plates) {
    return (plates >= 20) ? 3 : 2;
}

// ---- per-plate round-robin tracker (fixed table, no allocation) ------------
// Nameplates are few (<= ~40 in 3.3.5a); 256 slots is ample. A stale/colliding
// entry can only cause one extra or one skipped update - never unsafe.
struct Slot { void* key; uint32_t frame; float accumDt; };
static const uint32_t SLOT_COUNT = 256;          // power of two
static const uint32_t SLOT_MASK  = SLOT_COUNT - 1;
static Slot g_slots[SLOT_COUNT] = {};

static inline uint32_t HashPtr(void* p) {
    uint32_t x = (uint32_t)(uintptr_t)p;
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15;
    return x & SLOT_MASK;
}

// ---- crowd detection (per frame) -------------------------------------------
static uint32_t g_lastFrame     = 0;
static uint32_t g_countThisFrame = 0;   // main-thread only, no atomics needed
static uint32_t g_countLastFrame = 0;
static bool     g_crowded        = false;

// ---- stats -----------------------------------------------------------------
static uint64_t g_throttledTotal = 0;
static uint64_t g_updatedTotal   = 0;

static inline bool ValidPtr(uintptr_t p) { return p >= 0x10000 && p < 0xFFE00000; }

static void __fastcall Hooked_Update(void* pThis, void* edx, float dt)
{
    if (!ValidPtr((uintptr_t)pThis)) {
        if (orig_Update) orig_Update(pThis, edx, dt);
        return;
    }

    __try {
        uintptr_t self = (uintptr_t)pThis;

        // Roll the crowd counter on a new frame.
        uint32_t frame = *g_frameCounter;
        if (frame != g_lastFrame) {
            g_countLastFrame = g_countThisFrame;
            g_countThisFrame = 0;
            g_crowded = (g_countLastFrame >= g_crowdThreshold);
            g_lastFrame = frame;
        }
        g_countThisFrame++;

        // Scene is light, or this is the current target -> always full rate.
        uint64_t guid = *(uint64_t*)(self + 680);
        if (!g_crowded || (guid != 0 && guid == *g_targetGuid)) {
            g_updatedTotal++;
            orig_Update(pThis, edx, dt);
            return;
        }

        uint32_t interval = IntervalForCrowd(g_countLastFrame);
        uint32_t h = HashPtr(pThis);
        Slot& s = g_slots[h];

        if (s.key != pThis) {
            // First sight (or a collision evicted the slot): register with a
            // per-plate phase offset so plates don't all update on the same
            // frame, and update now.
            uint32_t phase = HashPtr((void*)~(uintptr_t)pThis) % (interval ? interval : 1);
            s.key = pThis;
            s.frame = frame - phase;
            s.accumDt = 0.0f;
            g_updatedTotal++;
            orig_Update(pThis, edx, dt);
            return;
        }

        if ((uint32_t)(frame - s.frame) >= interval) {
            // Due: update with the dt accumulated across the skipped frames so
            // its timers/animations advance by the correct total time.
            float total = s.accumDt + dt;
            s.frame = frame;
            s.accumDt = 0.0f;
            g_updatedTotal++;
            orig_Update(pThis, edx, total);
            return;
        }

        // Throttle: skip this frame's update, banking dt for when it's next due.
        s.accumDt += dt;
        g_throttledTotal++;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (orig_Update) orig_Update(pThis, edx, dt);
    }
}

static void* __fastcall Hooked_GetWorldMatrix(void* pThis, void* edx, void* a2)
{
    if (pThis) {
        void* vtable = *(void**)pThis;
        // Verify if this is indeed a Nameplate Frame object to safely query the GUID field.
        if (vtable == (void*)0x00A3278C || vtable == (void*)0x00A34E54) {
            uintptr_t self = (uintptr_t)pThis;
            uint64_t* pGuid = (uint64_t*)(self + 680);
            uint64_t originalGuid = *pGuid;

            if (originalGuid != 0) {
                typedef void* (__cdecl* fn_4D4DB0)(uint64_t guid, int typeMask);
                fn_4D4DB0 typeCheck = (fn_4D4DB0)0x004D4DB0;

                void* unit = nullptr;
                __try {
                    unit = typeCheck(originalGuid, 1);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    unit = nullptr;
                }

                if (!unit) {
                    // The unit represented by this nameplate has despawned and been freed.
                    // Temporarily set the nameplate GUID to 0 so the original GetWorldMatrix
                    // function skips the virtual call matrix adjustment that would otherwise crash.
                    *pGuid = 0;
                    void* result = orig_GetWorldMatrix(pThis, edx, a2);
                    *pGuid = originalGuid;
                    return result;
                }
            }
        }
    }
    return orig_GetWorldMatrix(pThis, edx, a2);
}

bool Init()
{
    if (!Config::g_settings.OptNameplateThrottle) {
        Log("[NameplateThrottle] disabled via configuration (opt-in)");
        return true;
    }

    // NameplateMT (nameplate_batch.cpp) hooks the same function; it is
    // compile-disabled (TEST_DISABLE_NAMEPLATE_MT), but guard defensively so we
    // never double-hook 0x0098E9F0.
#if !TEST_DISABLE_NAMEPLATE_MT
    Log("[NameplateThrottle] NOT installing: NameplateMT owns 0x0098E9F0");
    return false;
#endif

    void* target = (void*)0x0098E9F0;
    unsigned char* p = (unsigned char*)target;
    if (p[0] != 0x55 || p[1] != 0x8B || p[2] != 0xEC) {   // push ebp; mov ebp,esp
        Log("[NameplateThrottle] BAD PROLOGUE at 0x0098E9F0 (got %02X %02X %02X) - not installing",
            p[0], p[1], p[2]);
        return false;
    }
    if (MH_CreateHook(target, (void*)Hooked_Update, (void**)&orig_Update) != MH_OK) {
        Log("[NameplateThrottle] MH_CreateHook FAILED");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[NameplateThrottle] MH_EnableHook FAILED");
        return false;
    }
    g_installed = true;

    // GetWorldMatrix detour hook for 0x00722C14 crash protection
    void* matrixTarget = (void*)0x00722B50;
    unsigned char* pMatrix = (unsigned char*)matrixTarget;
    if (pMatrix[0] != 0x55 || pMatrix[1] != 0x8B || pMatrix[2] != 0xEC) {
        Log("[NameplateThrottle] BAD PROLOGUE at 0x00722B50 (got %02X %02X %02X) - matrix hook skipped",
            pMatrix[0], pMatrix[1], pMatrix[2]);
    } else {
        if (MH_CreateHook(matrixTarget, (void*)Hooked_GetWorldMatrix, (void**)&orig_GetWorldMatrix) != MH_OK) {
            Log("[NameplateThrottle] MH_CreateHook FAILED for GetWorldMatrix");
        } else if (MH_EnableHook(matrixTarget) != MH_OK) {
            Log("[NameplateThrottle] MH_EnableHook FAILED for GetWorldMatrix");
        } else {
            g_installed_matrix = true;
            Log("[NameplateThrottle] GetWorldMatrix hook ACTIVE (0x00722B50 crash protection)");
        }
    }

    Log("[NameplateThrottle] ACTIVE: non-target nameplate updates throttled (engage>=%u plates, "
        "1/2-1/3 frames, target always full rate). Positioning untouched; only cosmetic refresh throttled.",
        g_crowdThreshold);
    return true;
}

void LogStats()
{
    if (!g_installed) return;
    uint64_t total = g_updatedTotal + g_throttledTotal;
    if (total == 0) return;
    Log("[NameplateThrottle] Stats: %llu updates run, %llu throttled (%.1f%% skipped), last frame had %u plates",
        (unsigned long long)g_updatedTotal, (unsigned long long)g_throttledTotal,
        100.0 * (double)g_throttledTotal / (double)total, g_countLastFrame);
}

void Shutdown()
{
    if (!g_installed) return;
    LogStats();
    MH_DisableHook((void*)0x0098E9F0);
    MH_RemoveHook((void*)0x0098E9F0);
    g_installed = false;

    if (g_installed_matrix) {
        MH_DisableHook((void*)0x00722B50);
        MH_RemoveHook((void*)0x00722B50);
        g_installed_matrix = false;
    }
}

} // namespace NameplateThrottle
