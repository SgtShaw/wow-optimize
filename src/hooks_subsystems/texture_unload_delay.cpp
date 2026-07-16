#include "texture_unload_delay.h"
#include "MinHook.h"
#include "config.h"
#include <vector>
#include <string>
#include "win_mutex.h"

extern "C" void Log(const char* fmt, ...);

namespace TextureUnloadDelay {
    static bool g_enabled = false;
    static bool g_isDeleting = false;
    static WinMutex g_textureLock;

    struct DelayedTexture {
        void* ptr;
        std::string path;
        DWORD timestamp;
    };

    static std::vector<DelayedTexture> g_delayedQueue;

    // Hook Target Types
    typedef void* (__thiscall *Texture_Delete_fn)(void* This, char a2);
    static Texture_Delete_fn orig_Texture_Delete = nullptr;

    typedef void* (__cdecl *Texture_Lookup_fn)(const char* path, char* a2, int a3);
    static Texture_Lookup_fn orig_Texture_Lookup = nullptr;

    typedef int (__cdecl *Texture_Insert_fn)(void* Block, int a2);
    static Texture_Insert_fn orig_Texture_Insert = nullptr;

    typedef void* (__cdecl *Texture_AddRef_fn)(void* Block);
    static Texture_AddRef_fn orig_Texture_AddRef = nullptr;

    static void DeleteTexture(void* ptr) {
        g_isDeleting = true;
        __try {
            orig_Texture_Delete(ptr, 1);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("[TextureUnloadDelay] Exception during texture deletion of 0x%p", ptr);
        }
        g_isDeleting = false;
    }

    static void EvictOldest() {
        if (g_delayedQueue.empty()) return;
        void* ptr = g_delayedQueue.front().ptr;
        g_delayedQueue.erase(g_delayedQueue.begin());
        DeleteTexture(ptr);
    }

    // Detour for HTEXTURE scalar deleting destructor
    void* __fastcall Hooked_Texture_Delete(void* Block, void* dummyEDX, char a2) {
        if (g_enabled && Block && a2 == 1) {
            WinLockGuard lock(g_textureLock);
            
            if (g_isDeleting) {
                return orig_Texture_Delete(Block, a2);
            }
            
            // Limit delayed queue size to 512 entries to manage memory overhead
            if (g_delayedQueue.size() >= 512) {
                EvictOldest();
            }

            DelayedTexture entry;
            entry.ptr = Block;
            entry.path = (const char*)Block + 108; // File path is offset 108 in HTEXTURE
            entry.timestamp = GetTickCount();
            g_delayedQueue.push_back(entry);
            
            return Block;
        }
        return orig_Texture_Delete(Block, a2);
    }

    // Detour for Texture Lookup
    void* __cdecl Hooked_Texture_Lookup(const char* path, char* a2, int a3) {
        void* result = orig_Texture_Lookup(path, a2, a3);
        if (result) {
            return result;
        }
        
        if (g_enabled && path) {
            WinLockGuard lock(g_textureLock);
            for (auto it = g_delayedQueue.begin(); it != g_delayedQueue.end(); ++it) {
                if (_stricmp(it->path.c_str(), path) == 0) {
                    void* Block = it->ptr;
                    g_delayedQueue.erase(it);
                    
                    // Re-insert texture back into the game's active hash map
                    orig_Texture_Insert(Block, *(int*)((char*)Block + 88));
                    
                    // Increment reference count to prevent immediate destruction
                    orig_Texture_AddRef(Block);
                    
                    if (a2) {
                        *a2 = 46; // Signal a valid dot suffix (matches original lookup logic)
                    }
                    
                    return Block;
                }
            }
        }
        
        return nullptr;
    }

    bool Init() {
        g_enabled = false;
        Log("[TextureUnloadDelay] DISABLED permanently for stability");
        return true;
    }

    void Shutdown() {
        if (!g_enabled) return;
        
        void* target_Delete = (void*)0x004B91D0;
        void* target_Lookup = (void*)0x004B6FA0;
        
        MH_DisableHook(target_Delete);
        MH_DisableHook(target_Lookup);
        
        WinLockGuard lock(g_textureLock);
        while (!g_delayedQueue.empty()) {
            void* ptr = g_delayedQueue.back().ptr;
            g_delayedQueue.pop_back();
            DeleteTexture(ptr);
        }
        
        Log("[TextureUnloadDelay] Shutdown - All delayed textures cleaned up");
    }

    void OnFrame() {
        if (!g_enabled) return;
        
        DWORD now = GetTickCount();
        WinLockGuard lock(g_textureLock);
        
        auto it = g_delayedQueue.begin();
        while (it != g_delayedQueue.end()) {
            if (now - it->timestamp >= 5000) { // 5-second Grace Period (TTL)
                void* ptr = it->ptr;
                it = g_delayedQueue.erase(it);
                DeleteTexture(ptr);
            } else {
                ++it;
            }
        }
    }
}
