#include "unit_aura_coalesce.h"
#include "MinHook.h"
#include "version.h"
#include <atomic>

extern "C" void Log(const char* fmt, ...);

namespace UnitAuraCoalesce {

typedef void (__thiscall *OnAuraUpdate_fn)(void* This, int a2, int a3);
static OnAuraUpdate_fn orig_OnAuraUpdate = nullptr;

struct AuraUpdateEntry {
    uint64_t guid;
    DWORD lastUpdateTick;
};

static constexpr int CACHE_SIZE = 512;
static constexpr int CACHE_MASK = CACHE_SIZE - 1;
static AuraUpdateEntry g_updateCache[CACHE_SIZE] = {};

static inline unsigned int HashGuid(uint64_t guid) {
    return (unsigned int)(guid ^ (guid >> 32)) & CACHE_MASK;
}

static void __fastcall Hooked_OnAuraUpdate(void* This, void* unused_edx, int a2, int a3) {
    #if !TEST_DISABLE_UNIT_AURA_COALESCE
    if (This) {
        // GUID is at offset 8 in WoW's CGObject / CGUnit_C base
        uint64_t guid = *(uint64_t*)((uintptr_t)This + 8);
        uint64_t playerGuid = *(uint64_t*)0x00BD07A0;
        
        if (guid != 0 && guid != playerGuid) {
            uint64_t targetGuid = *(uint64_t*)0x00BD07B0;
            if (guid != targetGuid) {
                unsigned int slot = HashGuid(guid);
                DWORD now = GetTickCount();
                if (g_updateCache[slot].guid == guid) {
                    if (now - g_updateCache[slot].lastUpdateTick < 100) {
                        return;
                    }
                }
                g_updateCache[slot].guid = guid;
                g_updateCache[slot].lastUpdateTick = now;
            }
        }
    }
    #endif
    orig_OnAuraUpdate(This, a2, a3);
}

bool Init() {
    void* target = (void*)0x0052F330;
    
    unsigned char prologue[3];
    __try {
        memcpy(prologue, target, 3);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("[UnitAuraCoalesce] Target 0x0052F330 not readable.");
        return true;
    }

    if (prologue[0] != 0x55 || prologue[1] != 0x8B || prologue[2] != 0xEC) {
        Log("[UnitAuraCoalesce] BAD PROLOGUE at 0x0052F330. Skipping hook.");
        return true;
    }

    if (MH_CreateHook(target, (void*)Hooked_OnAuraUpdate, (void**)&orig_OnAuraUpdate) == MH_OK) {
        if (MH_EnableHook(target) == MH_OK) {
            Log("[UnitAuraCoalesce] Detour active at 0x0052F330.");
            return true;
        }
        MH_RemoveHook(target);
    }
    return false;
}

void Shutdown() {
    MH_DisableHook((void*)0x0052F330);
}

} // namespace UnitAuraCoalesce
