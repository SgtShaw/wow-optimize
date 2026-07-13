#include "rcu_obj_mgr.h"
#include "core/config.h"
#include "MinHook.h"
#include "version.h"
#include <windows.h>
#include <atomic>
#include "win_mutex.h"
#include <intrin.h>

extern "C" void Log(const char* fmt, ...);

namespace RcuObjMgr {

struct RcuObjectArray {
    uint32_t count;
    void* objects[2048];
};

static std::atomic<RcuObjectArray*> g_rcuArray{nullptr};
static std::atomic<RcuObjectArray*> g_oldArrays[16]{nullptr};

// Offsets and hooks
typedef void* (__thiscall *LinkNode_fn)(void* This, int node);
static LinkNode_fn orig_LinkNode = nullptr;

typedef int (__cdecl *ClntObjMgrEnum_fn)(int (__cdecl *callback)(uint32_t, uint32_t, int), int context);
static ClntObjMgrEnum_fn orig_ClntObjMgrEnum = nullptr;

inline void* GetActiveObjMgr() {
    uintptr_t** tls = *(uintptr_t***)__readfsdword(0x2C);
    if (!tls) return nullptr;
    uint32_t* tlsIndexPtr = (uint32_t*)0x00D439BC;
    if (!tlsIndexPtr) return nullptr;
    uint32_t tlsIndex = *tlsIndexPtr;
    uintptr_t* tlsBlock = (uintptr_t*)tls[tlsIndex];
    if (!tlsBlock) return nullptr;
    return (void*)tlsBlock[2]; // Offset 8
}

void UpdateRcuArray(void* objMgr) {
    if (!objMgr) return;

    RcuObjectArray* newArray = new RcuObjectArray();
    newArray->count = 0;

    __try {
        uintptr_t firstObj = *(uintptr_t*)((char*)objMgr + 172);
        uintptr_t linkOffset = *(uintptr_t*)((char*)objMgr + 164);
        uintptr_t current = firstObj;

        while (current && (current & 1) == 0) {
            if (newArray->count < 2048) {
                newArray->objects[newArray->count++] = (void*)current;
            } else {
                break;
            }
            current = *(uintptr_t*)(current + linkOffset + 4);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Fallback in case of invalid pointer traversal during transitions
        delete newArray;
        return;
    }

    RcuObjectArray* oldArray = g_rcuArray.exchange(newArray, std::memory_order_release);

    if (oldArray) {
        bool queued = false;
        for (int i = 0; i < 16; i++) {
            RcuObjectArray* expected = nullptr;
            if (g_oldArrays[i].compare_exchange_strong(expected, oldArray)) {
                queued = true;
                break;
            }
        }
        if (!queued) {
            delete oldArray;
        }
    }
}

static void* __fastcall Hooked_LinkNode(void* This, void* unused, int node) {
    void* result = orig_LinkNode(This, node);
    void* activeMgr = GetActiveObjMgr();
    if (activeMgr && This == (void*)((char*)activeMgr + 164)) {
        UpdateRcuArray(activeMgr);
    }
    return result;
}

static int __cdecl Hooked_ClntObjMgrEnum(int (__cdecl *callback)(uint32_t, uint32_t, int), int context) {
    RcuObjectArray* arr = g_rcuArray.load(std::memory_order_acquire);
    if (arr) {
        for (uint32_t i = 0; i < arr->count; i++) {
            void* obj = arr->objects[i];
            if (obj) {
                __try {
                    uint32_t* j = (uint32_t*)obj;
                    uint32_t guidLow = j[12];
                    uint32_t guidHigh = j[13];
                    if (!callback(guidLow, guidHigh, context)) {
                        return 0;
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    // Safe handling of race condition or use-after-free
                }
            }
        }
        return 1;
    }
    return orig_ClntObjMgrEnum(callback, context);
}

bool Init() {
    if (!Config::g_settings.OptRcuObjMgr) {
        Log("[RcuObjMgr] DISABLED via configuration");
        return true;
    }
    Log("[RcuObjMgr] Init");

    for (int i = 0; i < 16; i++) {
        g_oldArrays[i].store(nullptr);
    }
    g_rcuArray.store(nullptr);

    void* linkTarget = (void*)0x006DED60;
    void* enumTarget = (void*)0x004D4B30;

    // Check prologues
    unsigned char linkPrologue[2];
    unsigned char enumPrologue[2];
    __try {
        memcpy(linkPrologue, linkTarget, 2);
        memcpy(enumPrologue, enumTarget, 2);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("[RcuObjMgr] Target memory not readable");
        return false;
    }

    if (WineSafe_CreateHook(linkTarget, (void*)Hooked_LinkNode, (void**)&orig_LinkNode) != MH_OK) {
        Log("[RcuObjMgr] Failed to hook LinkNode");
        return false;
    }
    if (WO_EnableHook(linkTarget) != MH_OK) {
        Log("[RcuObjMgr] Failed to enable LinkNode hook");
        return false;
    }

    if (WineSafe_CreateHook(enumTarget, (void*)Hooked_ClntObjMgrEnum, (void**)&orig_ClntObjMgrEnum) != MH_OK) {
        Log("[RcuObjMgr] Failed to hook ClntObjMgrEnum");
        return false;
    }
    if (WO_EnableHook(enumTarget) != MH_OK) {
        Log("[RcuObjMgr] Failed to enable ClntObjMgrEnum hook");
        return false;
    }

    Log("[RcuObjMgr] Active - Lock-free entity traverser initialized");
    return true;
}

void Shutdown() {
    if (!Config::g_settings.OptRcuObjMgr) return;
    void* linkTarget = (void*)0x006DED60;
    void* enumTarget = (void*)0x004D4B30;

    MH_DisableHook(linkTarget);
    MH_RemoveHook(linkTarget);

    MH_DisableHook(enumTarget);
    MH_RemoveHook(enumTarget);

    RcuObjectArray* arr = g_rcuArray.exchange(nullptr);
    if (arr) delete arr;

    for (int i = 0; i < 16; i++) {
        RcuObjectArray* old = g_oldArrays[i].exchange(nullptr);
        if (old) delete old;
    }
}

void OnFrame() {
    if (!Config::g_settings.OptRcuObjMgr) return;
    for (int i = 0; i < 16; i++) {
        RcuObjectArray* old = g_oldArrays[i].exchange(nullptr);
        if (old) {
            delete old;
        }
    }
}

void UpdateActiveRcuArray() {
    if (!Config::g_settings.OptRcuObjMgr) return;
    void* activeMgr = GetActiveObjMgr();
    if (activeMgr) {
        UpdateRcuArray(activeMgr);
    }
}

} // namespace RcuObjMgr
