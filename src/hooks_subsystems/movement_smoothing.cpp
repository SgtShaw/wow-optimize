#include "movement_smoothing.h"
#include "MinHook.h"
#include <unordered_map>

extern "C" void Log(const char* fmt, ...);

namespace MovementSmoothing {
    struct PositionHistory {
        float lastX, lastY, lastZ;
        float targetX, targetY, targetZ;
        DWORD lastUpdateTime;
    };

    static std::unordered_map<void*, PositionHistory> g_history;
    static SRWLOCK g_smoothingLock = SRWLOCK_INIT;
    static bool g_enabled = true;

    // Hook CGUnit_C::SetPosition at 0x00613E90 in WotLK 3.3.5a
    typedef void (__thiscall *SetPosition_fn)(void* This, const float* pos, float rotation);
    static SetPosition_fn orig_SetPosition = nullptr;

    void __fastcall Hooked_SetPosition(void* This, void* dummyEDX, const float* pos, float rotation) {
        if (g_enabled && This && pos) {
            float smoothedPos[3] = { pos[0], pos[1], pos[2] };
            SmoothPosition(This, &smoothedPos[0], &smoothedPos[1], &smoothedPos[2]);
            if (orig_SetPosition) {
                orig_SetPosition(This, smoothedPos, rotation);
                return;
            }
        }
        if (orig_SetPosition) {
            orig_SetPosition(This, pos, rotation);
        }
    }

    bool Init() {
        AcquireSRWLockExclusive(&g_smoothingLock);
        g_history.clear();
        ReleaseSRWLockExclusive(&g_smoothingLock);

        // Install hook on 0x00613E90
        void* target = reinterpret_cast<void*>(0x00613E90);
        if (MH_CreateHook(target, reinterpret_cast<void*>(&Hooked_SetPosition), reinterpret_cast<void**>(&orig_SetPosition)) == MH_OK) {
            MH_EnableHook(target);
            Log("[MovementSmoothing] Successfully hooked CGUnit_C::SetPosition at 0x%p", target);
            return true;
        }

        Log("[MovementSmoothing] Failed to hook CGUnit_C::SetPosition");
        return false;
    }

    void Shutdown() {
        void* target = reinterpret_cast<void*>(0x00613E90);
        MH_DisableHook(target);
        
        AcquireSRWLockExclusive(&g_smoothingLock);
        g_history.clear();
        ReleaseSRWLockExclusive(&g_smoothingLock);
    }

    void SmoothPosition(void* entity, float* x, float* y, float* z) {
        if (!g_enabled || !entity || !x || !y || !z) return;

        AcquireSRWLockExclusive(&g_smoothingLock);
        DWORD now = GetTickCount();

        auto it = g_history.find(entity);
        if (it == g_history.end()) {
            PositionHistory hist;
            hist.lastX = *x;
            hist.lastY = *y;
            hist.lastZ = *z;
            hist.targetX = *x;
            hist.targetY = *y;
            hist.targetZ = *z;
            hist.lastUpdateTime = now;
            g_history[entity] = hist;
            ReleaseSRWLockExclusive(&g_smoothingLock);
            return;
        }

        PositionHistory& hist = it->second;
        
        if (*x != hist.targetX || *y != hist.targetY || *z != hist.targetZ) {
            hist.lastX = *x;
            hist.lastY = *y;
            hist.lastZ = *z;
            hist.targetX = *x;
            hist.targetY = *y;
            hist.targetZ = *z;
            hist.lastUpdateTime = now;
        }

        DWORD elapsed = now - hist.lastUpdateTime;
        float t = elapsed / 100.0f;
        if (t > 1.0f) t = 1.0f;
        if (t < 0.0f) t = 0.0f;

        *x = hist.lastX + (hist.targetX - hist.lastX) * t;
        *y = hist.lastY + (hist.targetY - hist.lastY) * t;
        *z = hist.lastZ + (hist.targetZ - hist.lastZ) * t;
        ReleaseSRWLockExclusive(&g_smoothingLock);
    }
}
