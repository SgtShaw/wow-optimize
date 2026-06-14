/*
 * _strnicmp replacement
 * Target: 0x0076E780 (1013 callers)
 * int __stdcall(char*, char*, size_t) -> _strnicmp wrapper
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <atomic>
#include <cctype>
#include "MinHook.h"
#include "fast_strncmp.h"

extern "C" void Log(const char* fmt, ...);

typedef int (__stdcall *strnicmp_t)(const char*, const char*, size_t);
static strnicmp_t g_orig = nullptr;

static inline unsigned char to_lower_ascii(unsigned char c) {
    unsigned char is_upper = (unsigned char)((c - 'A') <= ('Z' - 'A'));
    return (unsigned char)(c | (is_upper << 5));
}

int __stdcall Hooked_strnicmp(const char* s1, const char* s2, size_t n) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    if (n == 0) return 0;
    if (s1 == s2) return 0;

    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;

    while (n--) {
        unsigned char c1 = *p1;
        unsigned char c2 = *p2;

        if (c1 == c2) {
            if (c1 == 0) return 0;
            p1++; p2++;
            continue;
        }

        unsigned char lc1 = to_lower_ascii(c1);
        unsigned char lc2 = to_lower_ascii(c2);
        if (lc1 != lc2) {
            return (lc1 < lc2) ? -1 : 1;
        }

        if (c1 == 0) return 0;
        p1++; p2++;
    }
    return 0;
}

bool InstallFastStrncmp() {
    void* target = (void*)0x0076E780;

    if (MH_CreateHook(target, (void*)Hooked_strnicmp, (void**)&g_orig) != MH_OK) {
        Log("[FastStrnicmp] Failed to create hook at 0x0076E780");
        return false;
    }

    if (MH_EnableHook(target) != MH_OK) {
        Log("[FastStrnicmp] Failed to enable hook");
        MH_RemoveHook(target);
        return false;
    }

    Log("[FastStrnicmp] Installed: _strnicmp replacement (1013 callers)");
    return true;
}

void UninstallFastStrncmp() {
    MH_DisableHook((void*)0x0076E780);
    MH_RemoveHook((void*)0x0076E780);
}

void GetFastStrncmpStats(uint64_t* calls) {
    if (calls) *calls = 0;
}
