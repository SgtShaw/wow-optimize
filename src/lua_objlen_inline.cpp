#include "lua_objlen_inline.h"
#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

static volatile long g_objlenCalls = 0;
static volatile long g_objlenFast  = 0;

typedef int (__cdecl *lua_objlen_fn)(int L, int idx);
static lua_objlen_fn orig_objlen = nullptr;

// lua_objlen at 0x84E1C0: reads table->sizearray, pushes it as a number.
// Called by the # operator on every addon array-length check.
static int __cdecl Hooked_ObjLen(int L, int idx) {
    ++g_objlenCalls;
    __try {
        int* base = *(int**)(L + 0x10);
        int* top  = *(int**)(L + 0x0C);
        int* slot = nullptr;

        if (idx > 0) {
            slot = base + (idx - 1) * 4;
            if (slot >= top) goto fallback;
        } else if (idx >= -10000) {
            slot = top + idx * 4;
            if (slot < base) goto fallback;
        } else if (idx == -10002) {
            slot = base + 18 * 4;
        }

        if (!slot || (uintptr_t)slot < 0x10000 || (uintptr_t)slot > 0xBFFF0000)
            goto fallback;

        int tt = slot[2];
        if (tt != 5) goto fallback;  // not a table

        int table = slot[0];
        if (table < 0x10000 || table > 0xBFFF0000) goto fallback;

        int sizearray = *(int*)(table + 32);  // table->sizearray
        int taint     = slot[3];
        ++g_objlenFast;

        // Push the sizearray as a number
        DWORD* topPtr = *(DWORD**)(L + 0x0C);
        if (!topPtr || (uintptr_t)topPtr < 0x10000 || (uintptr_t)topPtr > 0xBFFF0000)
            goto fallback;

        double* num = (double*)topPtr;
        *num = (double)sizearray;
        topPtr[2] = 3;      // tt = LUA_TNUMBER
        topPtr[3] = taint;  // taint propagation
        *(DWORD**)(L + 0x0C) = topPtr + 4;

        if (taint && *(int*)0x00D413A0 && !*(int*)0x00D413A4)
            *(int*)0x00D4139C = taint;

        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
fallback:
    return orig_objlen(L, idx);
}

bool InstallLuaObjLenInline() {
    void* target = (void*)0x0084E1C0;
    unsigned char* p = (unsigned char*)target;
    if (p[0] != 0x55 || p[1] != 0x8B) {
        Log("[LuaObjLen] BAD PROLOGUE at 0x%08X", (uintptr_t)target);
        return false;
    }
    if (MH_CreateHook(target, (void*)Hooked_ObjLen, (void**)&orig_objlen) != MH_OK) {
        Log("[LuaObjLen] MH_CreateHook FAILED");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[LuaObjLen] MH_EnableHook FAILED");
        return false;
    }
    Log("[LuaObjLen] ACTIVE: inline lua_objlen (0x84E1C0)");
    CrashDumper::RegisterFeature("LuaObjLen");
    CrashDumper::FeatureSetActive("LuaObjLen", true);
    return true;
}

void UninstallLuaObjLenInline() {
    MH_DisableHook((void*)0x0084E1C0);
    MH_RemoveHook((void*)0x0084E1C0);
    CrashDumper::FeatureSetActive("LuaObjLen", false);
    LONG64 total = g_objlenCalls;
    LONG64 fast  = g_objlenFast;
    if (total > 0) {
        Log("[LuaObjLen] Stats: %lld calls, %lld inline (%.1f%%)",
            (long long)total, (long long)fast,
            100.0 * (double)fast / (double)total);
    }
}