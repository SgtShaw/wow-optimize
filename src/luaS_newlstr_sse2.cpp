// ================================================================
// SSE2 Vectorized luaS_newlstr (Stateless String Creation)
// ================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <intrin.h>
#include <emmintrin.h>
#include "MinHook.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

static constexpr uintptr_t ADDR_luaS_newlstr = 0x00856C80;

typedef void* (__cdecl *luaS_newlstr_t)(void* L, const char* str, size_t l);
static luaS_newlstr_t orig_luaS_newlstr = nullptr;

static uint32_t g_newlstr_calls = 0;
static uint32_t g_newlstr_fast_hits = 0;
static uint32_t g_newlstr_dead = 0;

void* __cdecl Hooked_luaS_newlstr(void* L, const char* str, size_t l) {
    g_newlstr_calls++;
    
    // Bounds check string length
    if (l >= (1 << 30)) {
        return orig_luaS_newlstr(L, str, l);
    }
    
    // global_State is at L + 0x14
    uintptr_t L_addr = (uintptr_t)L;
    uintptr_t globalState = *(uintptr_t*)(L_addr + 0x14);
    
    if (globalState < 0x10000) {
        return orig_luaS_newlstr(L, str, l);
    }
    
    // Compute Lua 5.1 string hash
    uint32_t h = (uint32_t)l;
    size_t step = (l >> 5) + 1;
    for (size_t l1 = l; l1 >= step; l1 -= step) {
        h = h ^ ((h << 5) + (h >> 2) + (uint8_t)str[l1 - 1]);
    }
    
    // stringtable is at globalState + 0x00
    uint32_t* hash_array = *(uint32_t**)(globalState + 0x00);
    int nsize = *(int*)(globalState + 0x08);
    
    if (!hash_array || nsize <= 0) {
        return orig_luaS_newlstr(L, str, l);
    }
    
    uint32_t bucket = h & (nsize - 1);
    uint32_t tstring = hash_array[bucket];

    // currentwhite lives at G+0x15 in this build (IDA-verified vs sub_856C80;
    // the dead/resurrect test reads *(BYTE*)(G+21)). Reading +0x14 was the bug.
    uint8_t currentwhite = *(uint8_t*)(globalState + 0x15);

    // Traverse collision chain — wrapped in SEH because a GC sweep
    // can modify the chain concurrently with FrameScript execution
    __try {
        while (tstring) {
            if (tstring < 0x10000 || tstring > 0xBFFF0000) break;

            // tstring+16 is length
            if (*(uint32_t*)(tstring + 16) == l) {
                // tstring+20 is string data
                const char* ts_data = (const char*)(tstring + 20);

                if (memcmp(ts_data, str, l) == 0) {
                    // Content match. Replicate the engine EXACTLY (sub_856C80 tail):
                    //   if (~currentwhite & marked & 3)  marked ^= 3;  // changewhite
                    //   return ts;
                    // i.e. if the interned string is "other-white" (would be swept
                    // = dead), resurrect it in place, then hand it back. marked is
                    // the byte at TString+0x09 (reading +5 was the bug that let a
                    // GC-dead string slip through as live -> use-after-free).
                    uint8_t marked = *(uint8_t*)(tstring + 9);
                    if (((uint8_t)(~currentwhite) & marked & 3) != 0) {
                        *(uint8_t*)(tstring + 9) = (uint8_t)(marked ^ 3);
                    }
                    g_newlstr_fast_hits++;
                    return (void*)tstring;
                }
            }
            // next pointer
            tstring = *(uint32_t*)(tstring + 0);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // Corrupted chain pointer — fall through to original
    }

    return orig_luaS_newlstr(L, str, l);
}

namespace LuaSNewlstr {
    bool Init() {
        Log("[luaS_newlstr] Initializing SSE2 Fast Path");
        
        unsigned char* p = (unsigned char*)ADDR_luaS_newlstr;
        if (p[0] != 0x55 || p[1] != 0x8B) {
            Log("[luaS_newlstr] BAD PROLOGUE at 0x%08X", ADDR_luaS_newlstr);
            return false;
        }

        if (WineSafe_CreateHook((void*)ADDR_luaS_newlstr, (void*)Hooked_luaS_newlstr, (void**)&orig_luaS_newlstr) != MH_OK) {
            Log("[luaS_newlstr] Failed to hook");
            return false;
        }
        
        if (MH_EnableHook((void*)ADDR_luaS_newlstr) != MH_OK) {
            Log("[luaS_newlstr] Failed to enable hook");
            return false;
        }
        
        Log("[luaS_newlstr] ACTIVE (Hooked at 0x%08X)", ADDR_luaS_newlstr);
        return true;
    }

    void Shutdown() {
        if (orig_luaS_newlstr) {
            MH_DisableHook((void*)ADDR_luaS_newlstr);
        }
        if (g_newlstr_calls > 0) {
            Log("[luaS_newlstr] Stats: Calls %u, Fast Hits %u (%.1f%%), Dead %u", 
                g_newlstr_calls, g_newlstr_fast_hits, 
                100.0 * g_newlstr_fast_hits / g_newlstr_calls, g_newlstr_dead);
        }
    }
}
