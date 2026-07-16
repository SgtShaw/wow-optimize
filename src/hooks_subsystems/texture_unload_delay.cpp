#include "texture_unload_delay.h"
#include "MinHook.h"
#include "version.h"
#include "config.h"
#include <vector>
#include <string>
#include "win_mutex.h"

extern "C" void Log(const char* fmt, ...);

namespace TextureUnloadDelay {
    static bool g_enabled = false;
    static thread_local bool g_isReleasing = false;
    static WinMutex g_textureLock;

    struct DelayedTexture {
        void* ptr;
        DWORD timestamp;
    };

    static std::vector<DelayedTexture> g_delayedQueue;

    // Hook Target Types
    // Texture_Release (sub_47BF30) decrements refcount at offset 4
    typedef int (__cdecl *Texture_Release_fn)(void* Block);
    static Texture_Release_fn orig_Texture_Release = nullptr;

    static void ReleaseTexture(void* ptr) {
        g_isReleasing = true;
        __try {
            orig_Texture_Release(ptr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("[TextureUnloadDelay] Exception during texture release of 0x%p", ptr);
        }
        g_isReleasing = false;
    }

    // Detour for Texture Release
    int __cdecl Hooked_Texture_Release(void* Block) {
        if (!Block) return 0;

        if (g_enabled && !g_isReleasing) {
            // Check if reference count is exactly 1
            // Refcount is at offset 4 in HTEXTURE
            int* refCount = (int*)((char*)Block + 4);
            
            if (*refCount == 1) {
                WinLockGuard lock(g_textureLock);
                
                // Check if already in queue to prevent duplicates
                bool alreadyInQueue = false;
                for (const auto& item : g_delayedQueue) {
                    if (item.ptr == Block) {
                        alreadyInQueue = true;
                        break;
                    }
                }
                
                if (!alreadyInQueue) {
                    // Cache the texture and keep refcount at 1 (prevent destruction)
                    DelayedTexture entry;
                    entry.ptr = Block;
                    entry.timestamp = GetTickCount();
                    g_delayedQueue.push_back(entry);
                    
                    return 0; // Return without deleting (keeps refcount at 1)
                }
            }
        }
        
        return orig_Texture_Release(Block);
    }

    bool Init() {
        if (!Config::g_settings.OptTextureUnloadDelay) {
            g_enabled = false;
            return true;
        }

        Log("[TextureUnloadDelay] Initializing Texture Smart Unload Delay...");

        void* target_Release = (void*)0x0047BF30;

        if (WineSafe_CreateHook(target_Release, (void*)Hooked_Texture_Release, (void**)&orig_Texture_Release) != MH_OK) {
            Log("[TextureUnloadDelay] Failed to hook Texture Release");
            return false;
        }

        if (WO_EnableHook(target_Release) != MH_OK) {
            Log("[TextureUnloadDelay] Failed to enable hooks");
            return false;
        }

        g_enabled = true;
        Log("[TextureUnloadDelay] ACTIVE (Delayed release queue, TTL: 5000ms)");
        return true;
    }

    void Shutdown() {
        if (!g_enabled) return;
        
        void* target_Release = (void*)0x0047BF30;
        MH_DisableHook(target_Release);
        
        std::vector<void*> toRelease;
        {
            WinLockGuard lock(g_textureLock);
            while (!g_delayedQueue.empty()) {
                toRelease.push_back(g_delayedQueue.back().ptr);
                g_delayedQueue.pop_back();
            }
        }
        
        for (void* ptr : toRelease) {
            ReleaseTexture(ptr);
        }
        
        Log("[TextureUnloadDelay] Shutdown - All delayed textures cleaned up");
    }

    void OnFrame() {
        if (!g_enabled) return;
        
        DWORD now = GetTickCount();
        std::vector<void*> toRelease;
        
        {
            WinLockGuard lock(g_textureLock);
            auto it = g_delayedQueue.begin();
            while (it != g_delayedQueue.end()) {
                // If the refcount was incremented by the engine in the meantime, it means the texture
                // was looked up and reused. In this case, we remove it from the queue and let the engine own it.
                int* refCount = (int*)((char*)it->ptr + 4);
                if (*refCount > 1) {
                    // Engine reused it, remove from queue without releasing (refcount remains > 1)
                    it = g_delayedQueue.erase(it);
                } else if (now - it->timestamp >= 5000) { // 5-second Grace Period
                    toRelease.push_back(it->ptr);
                    it = g_delayedQueue.erase(it);
                } else {
                    ++it;
                }
            }
        }
        
        // Release textures OUTSIDE of the lock to prevent deadlocks with background worker threads
        for (void* ptr : toRelease) {
            ReleaseTexture(ptr);
        }
    }
}
