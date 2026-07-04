// ============================================================================
// Module: frame_limiter.cpp
// Description: Custom high-precision hybrid frame rate limiter.
// Safety & Threading: Thread-safe. Integrates with the main render thread.
// ============================================================================

#include "frame_limiter.h"
#include "MinHook.h"
#include "version.h"
#include <windows.h>

extern "C" void Log(const char* fmt, ...);

namespace FrameLimiter {

thread_local bool g_bypassSleep = false;

typedef unsigned int (__thiscall *EngineFrameLimit_fn)(void* This);
static EngineFrameLimit_fn orig_EngineFrameLimit = nullptr;

typedef void (__cdecl *SleepHelper_fn)(DWORD ms);
static SleepHelper_fn orig_SleepHelper = nullptr;

// Detour for sub_86B280
void __cdecl Hooked_SleepHelper(DWORD ms) {
    if (g_bypassSleep) {
        return; // Bypass original sleep inside the frame limiter
    }
    orig_SleepHelper(ms);
}

// Detour for sub_6836D0
unsigned int __fastcall Hooked_EngineFrameLimit(void* This, void* unused) {
    if (!This) {
        return orig_EngineFrameLimit(This);
    }

#if !TEST_DISABLE_FRAME_LIMITER
    typedef int (__cdecl *GetLimit_fn)();
    GetLimit_fn GetFGLimit = (GetLimit_fn)0x00681780;
    GetLimit_fn GetBGLimit = (GetLimit_fn)0x006817A0;

    unsigned int fg = GetFGLimit();
    unsigned int bg = GetBGLimit();

    unsigned int limit = fg ? fg : -1;
    unsigned int bgLimit = bg ? bg : -1;

    // Check background state. Offset 3940 bytes from This is the active/foreground bool (985 * 4 = 3940)
    bool isBackground = !*((unsigned char*)This + 3940);
    if (isBackground && limit >= bgLimit) {
        limit = bgLimit;
    }

    if (limit <= 8 && limit != -1) {
        limit = 8;
    }

    if (limit == -1) {
        // No limit: bypass high precision wait
        g_bypassSleep = false;
        return orig_EngineFrameLimit(This);
    }

    double targetDuration = 1.0 / limit;

    static LARGE_INTEGER s_lastFrameTime = {0};
    static double s_ticksPerSec = 0.0;
    if (s_ticksPerSec == 0.0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        s_ticksPerSec = (double)freq.QuadPart;
        QueryPerformanceCounter(&s_lastFrameTime);
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double elapsed = (double)(now.QuadPart - s_lastFrameTime.QuadPart) / s_ticksPerSec;
    double remaining = targetDuration - elapsed;

    if (remaining > 0.0) {
        // Coarse sleep first to yield CPU cycles
        if (remaining > 0.002) {
            DWORD sleepMs = (DWORD)((remaining - 0.0015) * 1000.0);
            Sleep(sleepMs);
        }

        // Precision spin-yield loop
        LARGE_INTEGER targetTime;
        targetTime.QuadPart = s_lastFrameTime.QuadPart + (LONGLONG)(targetDuration * s_ticksPerSec);
        while (true) {
            QueryPerformanceCounter(&now);
            if (now.QuadPart >= targetTime.QuadPart) break;
            SwitchToThread();
        }
        s_lastFrameTime = targetTime;
    } else {
        if (remaining < -0.1) {
            s_lastFrameTime = now;
        } else {
            s_lastFrameTime.QuadPart += (LONGLONG)(targetDuration * s_ticksPerSec);
        }
    }
#endif

    // Run original frame limit updates with internal sleep bypassed
    g_bypassSleep = true;
    unsigned int ret = orig_EngineFrameLimit(This);
    g_bypassSleep = false;

    return ret;
}

bool Init() {
#if !TEST_DISABLE_FRAME_LIMITER
    void* target_limit = (void*)0x006836D0;
    void* target_sleep = (void*)0x0086B280;

    if (MH_CreateHook(target_limit, (void*)Hooked_EngineFrameLimit, (void**)&orig_EngineFrameLimit) != MH_OK ||
        MH_CreateHook(target_sleep, (void*)Hooked_SleepHelper, (void**)&orig_SleepHelper) != MH_OK) 
    {
        Log("[FrameLimiter] Failed to create hooks");
        return false;
    }

    MH_EnableHook(target_limit);
    MH_EnableHook(target_sleep);

    Log("[FrameLimiter] Active - High-precision hybrid frame rate limiter enabled");
#endif
    return true;
}

void Shutdown() {
#if !TEST_DISABLE_FRAME_LIMITER
    MH_DisableHook((void*)0x006836D0);
    MH_DisableHook((void*)0x0086B280);
#endif
}

} // namespace FrameLimiter
