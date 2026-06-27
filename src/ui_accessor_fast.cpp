#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "lua_optimize.h"
#include "ui_accessor_fast.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

static const uint32_t TAINT_CELL = 0x00D4139C;

static inline bool IsTeardownState() {
    uintptr_t gL = *(uintptr_t*)0x00D3F78C;
    return (gL < 0x10000 || gL > 0xBFFF0000);
}

static __forceinline bool IsValidPtr(uintptr_t p) {

    return p > 0x10000 && p < 0xBFFF0000;
}

static __forceinline uintptr_t ResolveTValue(uintptr_t L, int idx, bool* deferToOrig) {
    *deferToOrig = false;
    if (idx > 0) {
        uintptr_t base = *(uintptr_t*)(L + 0x10);
        if (!IsValidPtr(base)) return 0;
        uintptr_t tv = base + (uintptr_t)(idx - 1) * 16;
        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        if (tv >= top) return 0;
        return tv;
    }
    if (idx < 0 && idx > -10000) {
        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        if (!IsValidPtr(top)) return 0;
        uintptr_t tv = top + (uintptr_t)idx * 16;
        uintptr_t base = *(uintptr_t*)(L + 0x10);
        if (tv < base) return 0;
        return tv;
    }
    *deferToOrig = true;
    return 0;
}

static __forceinline void* GetCFrameFromLuaTable(uintptr_t L, int idx) {
    bool defer = false;
    uintptr_t tv = ResolveTValue(L, idx, &defer);
    if (defer || !tv) return nullptr;

    if (*(int*)(tv + 8) != 5) return nullptr; // LUA_TTABLE
    uintptr_t t = *(uintptr_t*)(tv + 0);
    if (!IsValidPtr(t)) return nullptr;

    uintptr_t nodes = *(uintptr_t*)(t + 0x14); // t->node
    if (!IsValidPtr(nodes)) return nullptr;

    uintptr_t node = nodes;
    while (node && IsValidPtr(node)) {
        int key_tt = *(int*)(node + 0x18);
        if (key_tt == 3) { // LUA_TNUMBER
            double key_val = *(double*)(node + 0x10);
            if (key_val == 0.0) {
                int val_tt = *(int*)(node + 0x08);
                if (val_tt == 2) { // LUA_TLIGHTUSERDATA
                    return *(void**)(node + 0x00);
                } else if (val_tt == 7) { // LUA_TUSERDATA
                    uintptr_t udata = *(uintptr_t*)(node + 0x00);
                    if (IsValidPtr(udata)) {
                        return *(void**)(udata + 24);
                    }
                }
                return nullptr;
            }
        }
        node = *(uintptr_t*)(node + 0x20); // node->next
    }
    return nullptr;
}

// Stats
static volatile long g_isshown_calls = 0;
static volatile long g_isvisible_calls = 0;
static volatile long g_getalpha_calls = 0;
static volatile long g_getscale_calls = 0;
static volatile long g_getwidth_calls = 0;
static volatile long g_getheight_calls = 0;
static volatile long g_frame_isshown_calls = 0;
static volatile long g_frame_isvisible_calls = 0;
static volatile long g_frame_getalpha_calls = 0;
static volatile long g_frame_getframelevel_calls = 0;

// Type checking helper
static __forceinline bool ValidateObjectType(void* obj, int typeId) {
    uintptr_t vtable = *(uintptr_t*)obj;
    if (!IsValidPtr(vtable)) return false;
    uintptr_t is_type_fn = *(uintptr_t*)(vtable + 16);
    if (!IsValidPtr(is_type_fn)) return false;
    return ((bool (__thiscall*)(void*, int))is_type_fn)(obj, typeId);
}

// 1. IsShown — 0x0048C610
typedef int (__cdecl* IsShown_t)(uintptr_t L);
static IsShown_t orig_IsShown = nullptr;

static int __cdecl hook_IsShown(uintptr_t L) {
    CrashDumper::RecordHookCall("UIAccessor_IsShown", (uintptr_t)L);
    if (IsTeardownState() || LuaOpt::IsReloading() || LuaOpt::IsSwapping()) 
        return orig_IsShown(L);

    ++g_isshown_calls;
    __try {
        int typeId = *(int*)0x00B4793C;
        if (typeId != 0) {
            void* obj = GetCFrameFromLuaTable(L, 1);
            if (obj && ValidateObjectType(obj, typeId)) {
                bool isShown = (*(uint8_t*)((uintptr_t)obj + 204) & 0x10) != 0;
                uintptr_t top = *(uintptr_t*)(L + 0x0C);
                if (IsValidPtr(top)) {
                    uint32_t taint = *(uint32_t*)TAINT_CELL;
                    if (isShown) {
                        *(uint32_t*)(top + 0) = 0;
                        *(uint32_t*)(top + 4) = 0x3FF00000; // double 1.0
                        *(int*)(top + 8) = 3; // LUA_TNUMBER
                        *(uint32_t*)(top + 12) = taint;
                    } else {
                        *(uint32_t*)(top + 0) = 0;
                        *(uint32_t*)(top + 4) = 0;
                        *(int*)(top + 8) = 0; // LUA_TNIL
                        *(uint32_t*)(top + 12) = taint;
                    }
                    *(uintptr_t*)(L + 0x0C) = top + 16;
                    return 1;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return orig_IsShown(L);
}

// 2. IsVisible — 0x0048C5B0
typedef int (__cdecl* IsVisible_t)(uintptr_t L);
static IsVisible_t orig_IsVisible = nullptr;

static int __cdecl hook_IsVisible(uintptr_t L) {
    CrashDumper::RecordHookCall("UIAccessor_IsVisible", (uintptr_t)L);
    if (IsTeardownState() || LuaOpt::IsReloading() || LuaOpt::IsSwapping()) 
        return orig_IsVisible(L);

    ++g_isvisible_calls;
    __try {
        int typeId = *(int*)0x00B4793C;
        if (typeId != 0) {
            void* obj = GetCFrameFromLuaTable(L, 1);
            if (obj && ValidateObjectType(obj, typeId)) {
                bool isVisible = (*(uint8_t*)((uintptr_t)obj + 204) & 0x20) != 0;
                uintptr_t top = *(uintptr_t*)(L + 0x0C);
                if (IsValidPtr(top)) {
                    uint32_t taint = *(uint32_t*)TAINT_CELL;
                    if (isVisible) {
                        *(uint32_t*)(top + 0) = 0;
                        *(uint32_t*)(top + 4) = 0x3FF00000; // double 1.0
                        *(int*)(top + 8) = 3;
                        *(uint32_t*)(top + 12) = taint;
                    } else {
                        *(uint32_t*)(top + 0) = 0;
                        *(uint32_t*)(top + 4) = 0;
                        *(int*)(top + 8) = 0;
                        *(uint32_t*)(top + 12) = taint;
                    }
                    *(uintptr_t*)(L + 0x0C) = top + 16;
                    return 1;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return orig_IsVisible(L);
}

// 3. GetAlpha — 0x0048C4C0
typedef int (__cdecl* GetAlpha_t)(uintptr_t L);
static GetAlpha_t orig_GetAlpha = nullptr;

static int __cdecl hook_GetAlpha(uintptr_t L) {
    CrashDumper::RecordHookCall("UIAccessor_GetAlpha", (uintptr_t)L);
    if (IsTeardownState() || LuaOpt::IsReloading() || LuaOpt::IsSwapping()) 
        return orig_GetAlpha(L);

    ++g_getalpha_calls;
    __try {
        int typeId = *(int*)0x00B4793C;
        if (typeId != 0) {
            void* obj = GetCFrameFromLuaTable(L, 1);
            if (obj && ValidateObjectType(obj, typeId)) {
                uint8_t alpha_byte = 255;
                if (*(int*)((uintptr_t)obj + 168) == 1) {
                    alpha_byte = *(uint8_t*)((uintptr_t)obj + 164);
                }
                double alpha_val = (double)alpha_byte * 0.00392156862745098;
                uintptr_t top = *(uintptr_t*)(L + 0x0C);
                if (IsValidPtr(top)) {
                    *(double*)(top + 0) = alpha_val;
                    *(int*)(top + 8) = 3; // LUA_TNUMBER
                    *(uint32_t*)(top + 12) = *(uint32_t*)TAINT_CELL;
                    *(uintptr_t*)(L + 0x0C) = top + 16;
                    return 1;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return orig_GetAlpha(L);
}

// 4. GetScale — 0x0049F7D0
typedef int (__cdecl* GetScale_t)(uintptr_t L);
static GetScale_t orig_GetScale = nullptr;

static int __cdecl hook_GetScale(uintptr_t L) {
    CrashDumper::RecordHookCall("UIAccessor_GetScale", (uintptr_t)L);
    if (IsTeardownState() || LuaOpt::IsReloading() || LuaOpt::IsSwapping()) 
        return orig_GetScale(L);

    ++g_getscale_calls;
    __try {
        int typeId = *(int*)0x00B49984; // Note different typeId global address
        if (typeId != 0) {
            void* obj = GetCFrameFromLuaTable(L, 1);
            if (obj && ValidateObjectType(obj, typeId)) {
                float scale = *(float*)((uintptr_t)obj + 184);
                uintptr_t top = *(uintptr_t*)(L + 0x0C);
                if (IsValidPtr(top)) {
                    *(double*)(top + 0) = (double)scale;
                    *(int*)(top + 8) = 3; // LUA_TNUMBER
                    *(uint32_t*)(top + 12) = *(uint32_t*)TAINT_CELL;
                    *(uintptr_t*)(L + 0x0C) = top + 16;
                    return 1;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return orig_GetScale(L);
}

// 5. GetWidth — 0x0049D3B0
typedef int (__cdecl* GetWidth_t)(uintptr_t L);
static GetWidth_t orig_GetWidth = nullptr;

static int __cdecl hook_GetWidth(uintptr_t L) {
    CrashDumper::RecordHookCall("UIAccessor_GetWidth", (uintptr_t)L);
    if (IsTeardownState() || LuaOpt::IsReloading() || LuaOpt::IsSwapping()) 
        return orig_GetWidth(L);

    ++g_getwidth_calls;
    __try {
        uintptr_t base = *(uintptr_t*)(L + 0x10);
        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        if (IsValidPtr(base) && IsValidPtr(top)) {
            int nargs = (top - base) / 16;
            if (nargs < 2) {
                int typeId = *(int*)0x00B49978;
                if (typeId != 0) {
                    void* obj = GetCFrameFromLuaTable(L, 1);
                    if (obj && ValidateObjectType(obj, typeId)) {
                        void* layoutFrame = (void*)((uintptr_t)obj + 32);
                        uintptr_t vtable = *(uintptr_t*)layoutFrame;
                        if (IsValidPtr(vtable)) {
                            typedef double (__thiscall* LayoutFrame_GetWidth_t)(void*);
                            LayoutFrame_GetWidth_t getWidthFn = *(LayoutFrame_GetWidth_t*)(vtable + 40);
                            if (IsValidPtr((uintptr_t)getWidthFn)) {
                                double w = getWidthFn(layoutFrame);
                                if (w != 0.0) {
                                    typedef double (*sub_47BFE0_t)();
                                    typedef double (*sub_47C050_t)(float);
                                    double scale = ((sub_47BFE0_t)0x0047BFE0)();
                                    double v7 = scale * 1024.0 * w;
                                    double final_w = ((sub_47C050_t)0x0047C050)((float)v7);

                                    typedef int (__cdecl* lua_pushnumber_t)(uintptr_t, double);
                                    ((lua_pushnumber_t)0x0084E2A0)(L, final_w);
                                    return 1;
                                }
                            }
                        }
                    }
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return orig_GetWidth(L);
}

// 6. GetHeight — 0x0049D550
typedef int (__cdecl* GetHeight_t)(uintptr_t L);
static GetHeight_t orig_GetHeight = nullptr;

static int __cdecl hook_GetHeight(uintptr_t L) {
    CrashDumper::RecordHookCall("UIAccessor_GetHeight", (uintptr_t)L);
    if (IsTeardownState() || LuaOpt::IsReloading() || LuaOpt::IsSwapping()) 
        return orig_GetHeight(L);

    ++g_getheight_calls;
    __try {
        uintptr_t base = *(uintptr_t*)(L + 0x10);
        uintptr_t top = *(uintptr_t*)(L + 0x0C);
        if (IsValidPtr(base) && IsValidPtr(top)) {
            int nargs = (top - base) / 16;
            if (nargs < 2) {
                int typeId = *(int*)0x00B49978;
                if (typeId != 0) {
                    void* obj = GetCFrameFromLuaTable(L, 1);
                    if (obj && ValidateObjectType(obj, typeId)) {
                        void* layoutFrame = (void*)((uintptr_t)obj + 32);
                        uintptr_t vtable = *(uintptr_t*)layoutFrame;
                        if (IsValidPtr(vtable)) {
                            typedef double (__thiscall* LayoutFrame_GetHeight_t)(void*);
                            LayoutFrame_GetHeight_t getHeightFn = *(LayoutFrame_GetHeight_t*)(vtable + 44);
                            if (IsValidPtr((uintptr_t)getHeightFn)) {
                                double h = getHeightFn(layoutFrame);
                                if (h != 0.0) {
                                    typedef double (*sub_47BFE0_t)();
                                    typedef double (*sub_47C050_t)(float);
                                    double scale = ((sub_47BFE0_t)0x0047BFE0)();
                                    double v7 = scale * 1024.0 * h;
                                    double final_h = ((sub_47C050_t)0x0047C050)((float)v7);

                                    typedef int (__cdecl* lua_pushnumber_t)(uintptr_t, double);
                                    ((lua_pushnumber_t)0x0084E2A0)(L, final_h);
                                    return 1;
                                }
                            }
                        }
                    }
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return orig_GetHeight(L);
}

// 7. Frame_IsShown — 0x0049FE90
typedef int (__cdecl* Frame_IsShown_t)(uintptr_t L);
static Frame_IsShown_t orig_Frame_IsShown = nullptr;

static int __cdecl hook_Frame_IsShown(uintptr_t L) {
    CrashDumper::RecordHookCall("UIAccessor_FrameIsShown", (uintptr_t)L);
    if (IsTeardownState() || LuaOpt::IsReloading() || LuaOpt::IsSwapping()) 
        return orig_Frame_IsShown(L);

    ++g_frame_isshown_calls;
    __try {
        int typeId = *(int*)0x00B49984;
        if (typeId != 0) {
            void* obj = GetCFrameFromLuaTable(L, 1);
            if (obj && ValidateObjectType(obj, typeId)) {
                bool isShown = *(int*)((uintptr_t)obj + 220) != 0;
                uintptr_t top = *(uintptr_t*)(L + 0x0C);
                if (IsValidPtr(top)) {
                    uint32_t taint = *(uint32_t*)TAINT_CELL;
                    if (isShown) {
                        *(uint32_t*)(top + 0) = 0;
                        *(uint32_t*)(top + 4) = 0x3FF00000; // double 1.0
                        *(int*)(top + 8) = 3; // LUA_TNUMBER
                        *(uint32_t*)(top + 12) = taint;
                    } else {
                        *(uint32_t*)(top + 0) = 0;
                        *(uint32_t*)(top + 4) = 0;
                        *(int*)(top + 8) = 0; // LUA_TNIL
                        *(uint32_t*)(top + 12) = taint;
                    }
                    *(uintptr_t*)(L + 0x0C) = top + 16;
                    return 1;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return orig_Frame_IsShown(L);
}

// 8. Frame_IsVisible — 0x0049FE30
typedef int (__cdecl* Frame_IsVisible_t)(uintptr_t L);
static Frame_IsVisible_t orig_Frame_IsVisible = nullptr;

static int __cdecl hook_Frame_IsVisible(uintptr_t L) {
    CrashDumper::RecordHookCall("UIAccessor_FrameIsVisible", (uintptr_t)L);
    if (IsTeardownState() || LuaOpt::IsReloading() || LuaOpt::IsSwapping()) 
        return orig_Frame_IsVisible(L);

    ++g_frame_isvisible_calls;
    __try {
        int typeId = *(int*)0x00B49984;
        if (typeId != 0) {
            void* obj = GetCFrameFromLuaTable(L, 1);
            if (obj && ValidateObjectType(obj, typeId)) {
                bool isVisible = *(int*)((uintptr_t)obj + 224) != 0;
                uintptr_t top = *(uintptr_t*)(L + 0x0C);
                if (IsValidPtr(top)) {
                    uint32_t taint = *(uint32_t*)TAINT_CELL;
                    if (isVisible) {
                        *(uint32_t*)(top + 0) = 0;
                        *(uint32_t*)(top + 4) = 0x3FF00000; // double 1.0
                        *(int*)(top + 8) = 3;
                        *(uint32_t*)(top + 12) = taint;
                    } else {
                        *(uint32_t*)(top + 0) = 0;
                        *(uint32_t*)(top + 4) = 0;
                        *(int*)(top + 8) = 0;
                        *(uint32_t*)(top + 12) = taint;
                    }
                    *(uintptr_t*)(L + 0x0C) = top + 16;
                    return 1;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return orig_Frame_IsVisible(L);
}

// 9. Frame_GetAlpha — 0x0049F980
typedef int (__cdecl* Frame_GetAlpha_t)(uintptr_t L);
static Frame_GetAlpha_t orig_Frame_GetAlpha = nullptr;

static int __cdecl hook_Frame_GetAlpha(uintptr_t L) {
    CrashDumper::RecordHookCall("UIAccessor_FrameGetAlpha", (uintptr_t)L);
    if (IsTeardownState() || LuaOpt::IsReloading() || LuaOpt::IsSwapping()) 
        return orig_Frame_GetAlpha(L);

    ++g_frame_getalpha_calls;
    __try {
        int typeId = *(int*)0x00B49984;
        if (typeId != 0) {
            void* obj = GetCFrameFromLuaTable(L, 1);
            if (obj && ValidateObjectType(obj, typeId)) {
                double alpha_val = (double)*(unsigned __int8 *)((uintptr_t)obj + 188) * 0.00392156862745098;
                uintptr_t top = *(uintptr_t*)(L + 0x0C);
                if (IsValidPtr(top)) {
                    *(double*)(top + 0) = alpha_val;
                    *(int*)(top + 8) = 3; // LUA_TNUMBER
                    *(uint32_t*)(top + 12) = *(uint32_t*)TAINT_CELL;
                    *(uintptr_t*)(L + 0x0C) = top + 16;
                    return 1;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return orig_Frame_GetAlpha(L);
}

// 10. Frame_GetFrameLevel — 0x0049E980
typedef int (__cdecl* Frame_GetFrameLevel_t)(uintptr_t L);
static Frame_GetFrameLevel_t orig_Frame_GetFrameLevel = nullptr;

static int __cdecl hook_Frame_GetFrameLevel(uintptr_t L) {
    CrashDumper::RecordHookCall("UIAccessor_FrameGetLevel", (uintptr_t)L);
    if (IsTeardownState() || LuaOpt::IsReloading() || LuaOpt::IsSwapping()) 
        return orig_Frame_GetFrameLevel(L);

    ++g_frame_getframelevel_calls;
    __try {
        int typeId = *(int*)0x00B49984;
        if (typeId != 0) {
            void* obj = GetCFrameFromLuaTable(L, 1);
            if (obj && ValidateObjectType(obj, typeId)) {
                double level = (double)*(int*)((uintptr_t)obj + 212);
                uintptr_t top = *(uintptr_t*)(L + 0x0C);
                if (IsValidPtr(top)) {
                    *(double*)(top + 0) = level;
                    *(int*)(top + 8) = 3; // LUA_TNUMBER
                    *(uint32_t*)(top + 12) = *(uint32_t*)TAINT_CELL;
                    *(uintptr_t*)(L + 0x0C) = top + 16;
                    return 1;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return orig_Frame_GetFrameLevel(L);
}

// Install / Shutdown
bool InstallUIAccessorFast() {
    int installed = 0;
    
#define INSTALL(fn_t, orig_ptr, hook_ptr, addr, name) \
    if (WineSafe_CreateHook((void*)(addr), (void*)(hook_ptr), (void**)&(orig_ptr)) == MH_OK) { \
        if (WO_EnableHook((void*)(addr)) == MH_OK) { \
            installed++; \
            Log("[UIAccessorFast] Hooked " name " at 0x%08X", (DWORD)(addr)); \
        } else { \
            Log("[UIAccessorFast] Enable " name " FAILED"); \
        } \
    } else { \
        Log("[UIAccessorFast] Create " name " FAILED"); \
    }
#define INSTALL_GATED(fn_t, orig_ptr, hook_ptr, addr, name, flag) \
    do { \
        if (!(flag)) { \
            INSTALL(fn_t, orig_ptr, hook_ptr, addr, name) \
        } else { \
            Log("[UIAccessorFast] " name " DISABLED by flag (0x%08X)", (DWORD)(flag)); \
        } \
    } while(0)

    INSTALL(IsShown_t,    orig_IsShown,    hook_IsShown,    0x0048C610, "IsShown");
    INSTALL(IsVisible_t,  orig_IsVisible,  hook_IsVisible,  0x0048C5B0, "IsVisible");
    INSTALL(GetAlpha_t,   orig_GetAlpha,   hook_GetAlpha,   0x0048C4C0, "GetAlpha");
    INSTALL(GetScale_t,   orig_GetScale,   hook_GetScale,   0x0049F7D0, "GetScale");

    // NEW Frame XML accessor hooks (IDA-verified __cdecl, gated)
    INSTALL_GATED(Frame_IsShown_t,      orig_Frame_IsShown,      hook_Frame_IsShown,      0x0049FE90, "Frame_IsShown",      TEST_DISABLE_FRAME_ACCESSOR_FAST);
    INSTALL_GATED(Frame_IsVisible_t,    orig_Frame_IsVisible,    hook_Frame_IsVisible,    0x0049FE30, "Frame_IsVisible",    TEST_DISABLE_FRAME_ACCESSOR_FAST);
    INSTALL_GATED(Frame_GetAlpha_t,     orig_Frame_GetAlpha,     hook_Frame_GetAlpha,     0x0049F980, "Frame_GetAlpha",     TEST_DISABLE_FRAME_ACCESSOR_FAST);
    INSTALL_GATED(Frame_GetFrameLevel_t,orig_Frame_GetFrameLevel,hook_Frame_GetFrameLevel, 0x0049E980, "Frame_GetFrameLevel",TEST_DISABLE_FRAME_ACCESSOR_FAST);

    // Layout accessor hooks (IDA-verified __usercall — DISABLED until naked-asm trampoline)
    INSTALL_GATED(GetWidth_t,  orig_GetWidth,  hook_GetWidth,  0x0049D3B0, "GetWidth",  TEST_DISABLE_LAYOUT_ACCESSOR_FAST);
    INSTALL_GATED(GetHeight_t, orig_GetHeight, hook_GetHeight, 0x0049D550, "GetHeight", TEST_DISABLE_LAYOUT_ACCESSOR_FAST);

#undef INSTALL_GATED
#undef INSTALL

    Log("[UIAccessorFast] %d/10 hooks installed", installed);
    return installed > 0;
}

void ShutdownUIAccessorFast() {
    MH_DisableHook((void*)0x0048C610);
    MH_DisableHook((void*)0x0048C5B0);
    MH_DisableHook((void*)0x0048C4C0);
    MH_DisableHook((void*)0x0049F7D0);
    MH_DisableHook((void*)0x0049D3B0);
    MH_DisableHook((void*)0x0049D550);
    MH_DisableHook((void*)0x0049FE90);
    MH_DisableHook((void*)0x0049FE30);
    MH_DisableHook((void*)0x0049F980);
    MH_DisableHook((void*)0x0049E980);
    Log("[UIAccessorFast] Stats: IsShown=%ld IsVisible=%ld GetAlpha=%ld GetScale=%ld GetWidth=%ld GetHeight=%ld FrameIsShown=%ld FrameIsVisible=%ld FrameGetAlpha=%ld FrameGetFrameLevel=%ld",
        g_isshown_calls, g_isvisible_calls, g_getalpha_calls, g_getscale_calls,
        g_getwidth_calls, g_getheight_calls, g_frame_isshown_calls, g_frame_isvisible_calls, g_frame_getalpha_calls, g_frame_getframelevel_calls);
}
