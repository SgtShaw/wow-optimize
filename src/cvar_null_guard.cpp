// ================================================================
// crashfix_7668d2.cpp — Fix NULL this in sub_7668C0 at 0x007668D2
// ================================================================
// Crash: ACCESS_VIOLATION at 0x007668D2 reading 0x00000068
//
// Root cause: sub_7668C0 (string parsing/profiling function)
// is called with this=NULL during login screen cinematic rendering.
// The function tries to read *(this + 104) which faults.
//
// Fix: Hook the function, validate this pointer, return 1 if invalid.
// ================================================================

#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "cvar_null_guard.h"

extern "C" void Log(const char* fmt, ...);

typedef char (__fastcall *Sub7668C0_t)(void* ecx, void* edx, char* Str1, char a3, char a4, char a5, char a6);
static Sub7668C0_t g_orig7668C0 = nullptr;

static char __fastcall Hooked_7668C0(void* ecx, void* edx, char* Str1, char a3, char a4, char a5, char a6)
{
    uintptr_t p = (uintptr_t)ecx;
    if (p < 0x10000 || p > 0xFFE00000) {
        return 1;
    }
    uintptr_t ptrAt104 = *(uintptr_t*)((char*)ecx + 104);
    if (ptrAt104 != 0 && (ptrAt104 < 0x10000 || ptrAt104 > 0xFFE00000)) {
        return 1;
    }
    return g_orig7668C0(ecx, edx, Str1, a3, a4, a5, a6);
}

bool InstallCvarNullGuard()
{
    void* target = (void*)0x007668C0;
    unsigned char* p = (unsigned char*)target;
    if (p[0] != 0x55 || p[1] != 0x8B || p[2] != 0xEC) {
        Log("[CvarGuard] BAD PROLOGUE (got %02X %02X %02X)", p[0], p[1], p[2]);
        return false;
    }
    if (MH_CreateHook(target, (void*)Hooked_7668C0, (void**)&g_orig7668C0) != MH_OK) {
        Log("[CvarGuard] MH_CreateHook FAILED");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[CvarGuard] MH_EnableHook FAILED");
        return false;
    }
    Log("[CvarGuard] ACTIVE -- NULL this guard on sub_7668C0");
    return true;
}
