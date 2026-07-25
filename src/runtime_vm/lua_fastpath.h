#pragma once

// ============================================================================
// Module: lua_fastpath.h
// Description: Accelerates Lua runtime calls in `lua_fastpath.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================









#ifndef LUA_FASTPATH_H
#define LUA_FASTPATH_H

#include <windows.h>

// Forward declaration
typedef struct lua_State lua_State;

// True if WoW will accept `fn` as a lua_CFunction.
//
// The client validates every C function pointer handed to the Lua VM against
// [dword_D415B8, dword_D415BC) in sub_86B5A0, and kills the process outright with
// ERROR #134 "Invalid function pointer: %p" when it falls outside. That range
// covers Wow.exe's own code, so a pointer into this DLL is only accepted when
// something has widened it. Anything registering our functions into Lua must ask
// this first and skip the registration when it returns false - the failure mode is
// a fatal error box at login, which no SEH guard can catch.
bool LuaCFunctionAccepted(const void* fn);

namespace LuaFastPath {

// Phase 1: Hook string.format (hardcoded address, called during DLL init)
bool Init();

// Feature 4 (0x009013B0): TLS-cached Lua Stack Type Check
int OptimizeSub9013B0_LuaTypeCheckTLS(lua_State* L, int idx, int expectedType);
// Feature 14 (0x008A65E0): De-virtualized FrameScript Event Dispatcher
bool OptimizeSub8A65E0_FrameScriptDispatch(void* frameObj, int eventId, const char* argStr);
// Feature 15 (0x00608880): Fast Event Name Hash Cache for UNIT_PET
const char* OptimizeSub608880_UnitPetHash();
// Feature 22 (0x00832EA0): TLS-cached Lua Environment Lookup
void* OptimizeSub832EA0_LuaEnvTLS(lua_State* L);
// Feature 25 (0x00693E40): De-virtualized Lua Metatable Call Dispatcher
void OptimizeSub693E40_LuaMetatableDispatch(lua_State* L, void* tableObj, const char* eventName);

// Feature 36 (0x006E2E90): De-virtualized Lua String Formatting Fastpath
int OptimizeSub6E2E90_DevirtualizeStringFormat(lua_State* L, const char* fmt, int numArgs);

// Phase 2: Discover and hook more functions at runtime (called after Lua state ready)
bool InitPhase2(lua_State* L);
// Allow Phase 2 discovery to re-run after lua_State / VM change
void ResetPhase2Discovery();

// Phase 3: WoW C-level API hooks (permanently disabled)
bool InitWoWHooks(lua_State* L);
void InvalidateWoWCache();

// Disable all hooks
void Shutdown();

struct Stats {
    long formatFastHits;
    long formatFallbacks;
    long findPlainHits;
    long findFallbacks;
    long matchHits;
    long matchFallbacks;
    long typeHits;
    long typeFallbacks;
    long mathHits;
    long mathFallbacks;
    long strlenHits;
    long strbyteHits;
    long tostringHits;
    long tostringFallbacks;
    long tonumberHits;
    long nextHits;
    long nextFallbacks;
    long rawgetHits;
    long rawgetFallbacks;
    long rawsetHits;
    long rawsetFallbacks;
    long tableInsertHits;
    long tableInsertFallbacks;
    long tableRemoveHits;
    long tableRemoveFallbacks;
    long tableConcatHits;           
    long tableConcatFallbacks;     
    long rawequalHits;          
    long rawequalFallbacks;     
    long unpackHits;           
    long unpackFallbacks;       
    long selectHits;
    long selectFallbacks;
    long strsubHits;
    long strlowerHits;
    long strupperHits;
    long ipairsHits;
    long ipairsFallbacks;
    long ipairsIteratorHits;
    long ipairsIteratorFallbacks;
    long findFullHits;
    long findFullFallbacks;
    long mathRandomHits;
    long mathRandomFallbacks;
    long mathSqrtHits;
    long mathSqrtFallbacks;
    long strRepHits;
    long strRepFallbacks;
    int  phase2Hooks;
    bool active;
    bool phase2Active;
    long unitHealthHits;
    long unitHealthFallbacks;
    long unitHealthMaxHits;
    long unitHealthMaxFallbacks;
    long unitPowerHits;
    long unitPowerFallbacks;
    long unitPowerMaxHits;
    long unitPowerMaxFallbacks;    
    long tableSortHits;
    long tableSortFallbacks;        
};

Stats GetStats();

} // namespace LuaFastPath

#endif