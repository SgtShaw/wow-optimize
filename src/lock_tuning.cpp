// Critical-section spin-count tuning. See lock_tuning.h for rationale.
//
// WoW links the MSVC CRT statically. __lock(n) (0x004121F8) resolves a lock as
// EnterCriticalSection(*(LPCRITICAL_SECTION*)(0x00AB6AB0 + 8*n)) -- i.e. 0x00AB6AB0
// is the CRT _locktable (8-byte entries {CRITICAL_SECTION* lock; long init;}). The
// global heap lock is index 4 (free() does _lock(4); _unlock(4) around __sbh ops),
// so every malloc/free/realloc/_msize on every thread serialises through that one
// critical section. The sound, file-I/O and main threads all allocate, so under
// load the main thread blocks behind them with a kernel wait. Giving these locks a
// spin count turns brief contended waits into userspace spins -- which is a win on
// the many-core CPUs WoW now runs on (a spare core spins essentially for free).
//
// This does NOT change locking semantics. SetCriticalSectionSpinCount only adjusts
// how long EnterCriticalSection spins before falling back to the kernel wait.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "lock_tuning.h"

extern "C" void Log(const char* fmt, ...);

// 4000 is the value Windows itself uses for the process-heap critical section; it
// is long enough to win the common brief-hold race without wasting many cycles.
static const DWORD SPIN_COUNT = 4000;

// CRT _locktable base (see header) and a conservative span to retrofit. Indices
// cover the heap lock (4), errno/signal, and the low stdio locks. We validate
// every pointer before touching it, so an out-of-range or not-yet-created slot is
// simply skipped.
static const uintptr_t CRT_LOCKTABLE = 0x00AB6AB0;
static const int       CRT_LOCK_SPAN = 32;

static unsigned g_retrofitted  = 0;
static unsigned g_runtimeTuned = 0;

typedef void (WINAPI *InitCS_fn)(LPCRITICAL_SECTION);
static InitCS_fn g_origInitCS = nullptr;

// A CRITICAL_SECTION is 24 bytes on x86. Require the whole structure to sit in one
// committed, writable region before we let SetCriticalSectionSpinCount write to it.
static bool IsTunableCS(void* p) {
    uintptr_t a = (uintptr_t)p;
    if (a < 0x10000 || a > 0xBFFFFFE0) return false;
    if (a & 3) return false;  // CRITICAL_SECTION is pointer-aligned
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(mbi.Protect & writable) || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    // Whole 24-byte structure must be inside this committed region.
    uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    return (a + sizeof(CRITICAL_SECTION)) <= regionEnd;
}

static bool TuneCS(LPCRITICAL_SECTION cs) {
    if (!IsTunableCS(cs)) return false;
    __try {
        SetCriticalSectionSpinCount(cs, SPIN_COUNT);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Hook: every critical section created after we install gets a spin count too.
// This catches engine locks created during play (per-connection, per-load, etc.).
static void WINAPI Hooked_InitializeCriticalSection(LPCRITICAL_SECTION cs) {
    g_origInitCS(cs);
    if (cs && SetCriticalSectionSpinCount(cs, SPIN_COUNT))
        ++g_runtimeTuned;
}

bool InstallLockTuning() {
    // 1. Retrofit the already-created CRT locks (the heap lock #4 is the prize).
    __try {
        for (int n = 0; n < CRT_LOCK_SPAN; ++n) {
            LPCRITICAL_SECTION* slot = (LPCRITICAL_SECTION*)(CRT_LOCKTABLE + (uintptr_t)n * 8);
            LPCRITICAL_SECTION cs = *slot;   // runtime-populated; NULL if lock n unused
            if (cs && TuneCS(cs))
                ++g_retrofitted;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[LockTuning] CRT lock-table sweep faulted (skipped)");
    }

    // 2. Blanket future critical sections via the kernel32 export.
    bool hookOk = false;
    if (MH_CreateHook((void*)&InitializeCriticalSection,
                      (void*)Hooked_InitializeCriticalSection,
                      (void**)&g_origInitCS) == MH_OK &&
        MH_EnableHook((void*)&InitializeCriticalSection) == MH_OK) {
        hookOk = true;
    }

    Log("[LockTuning] ACTIVE: spin=%u, CRT locks retrofitted=%u (heap lock #4 incl.), InitCS hook=%s",
        SPIN_COUNT, g_retrofitted, hookOk ? "ON" : "FAILED");
    return g_retrofitted > 0 || hookOk;
}

void GetLockTuningStats(unsigned* retrofitted, unsigned* runtimeTuned) {
    if (retrofitted)  *retrofitted  = g_retrofitted;
    if (runtimeTuned) *runtimeTuned = g_runtimeTuned;
}
