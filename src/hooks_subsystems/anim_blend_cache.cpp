#include "anim_blend_cache.h"
#include "MinHook.h"
#include <cmath>
#include "win_mutex.h"

extern "C" void Log(const char* fmt, ...);

namespace AnimBlendCache {
    struct CacheEntry {
        void* model;
        int boneIndex;
        float animTime;
        float matrix[16];
        bool valid;
    };

    static constexpr int CACHE_SIZE = 1024;
    static CacheEntry g_cache[CACHE_SIZE];
    static WinMutex g_cacheMutex;
    static bool g_enabled = true;

    // Hooking 0x005F91E0 (UpdateBones) in WotLK 3.3.5a
    typedef void (__thiscall *UpdateBones_fn)(void* This, void* a2);
    static UpdateBones_fn orig_UpdateBones = nullptr;

    void __fastcall Hooked_UpdateBones(void* This, void* dummyEDX, void* a2) {
        // Just call the original for safety, but this registers our hook presence
        if (orig_UpdateBones) {
            orig_UpdateBones(This, a2);
        }
    }

    bool Init() {
        Log("[AnimBlendCache] DISABLED (target address mismatch)");
        return false;
    }

    void Shutdown() {
        void* target = reinterpret_cast<void*>(0x005F91E0);
        MH_DisableHook(target);
        Clear();
    }

    void Clear() {
        WinLockGuard lock(g_cacheMutex);
        for (int i = 0; i < CACHE_SIZE; ++i) {
            g_cache[i].valid = false;
        }
    }

    inline unsigned int GetHash(void* model, int boneIndex, float animTime) {
        uintptr_t ptrVal = reinterpret_cast<uintptr_t>(model);
        int timeVal = static_cast<int>(animTime * 100.0f);
        return (unsigned int)((ptrVal ^ boneIndex) * 16777619 ^ timeVal) % CACHE_SIZE;
    }

    bool GetCachedMatrix(void* model, int boneIndex, float animTime, float* outMatrix) {
        if (!g_enabled) return false;

        WinLockGuard lock(g_cacheMutex);
        unsigned int idx = GetHash(model, boneIndex, animTime);
        if (g_cache[idx].valid && g_cache[idx].model == model && g_cache[idx].boneIndex == boneIndex) {
            if (std::abs(g_cache[idx].animTime - animTime) < 0.01f) {
                memcpy(outMatrix, g_cache[idx].matrix, sizeof(float) * 16);
                return true;
            }
        }
        return false;
    }

    void AddToCache(void* model, int boneIndex, float animTime, const float* matrix) {
        if (!g_enabled) return;

        WinLockGuard lock(g_cacheMutex);
        unsigned int idx = GetHash(model, boneIndex, animTime);
        g_cache[idx].model = model;
        g_cache[idx].boneIndex = boneIndex;
        g_cache[idx].animTime = animTime;
        memcpy(g_cache[idx].matrix, matrix, sizeof(float) * 16);
        g_cache[idx].valid = true;
    }
}
