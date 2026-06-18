// ================================================================
// luaH_newkey Safety Patch - Crash Fix for sub_85CAB0
// ================================================================
// Fixes the recurring ERROR #132 ACCESS_VIOLATION at 0x0085CB43 that
// reads address 0x00000020 (a NULL node + the 0x20 "next" field) on
// character login and on ESC > Exit Game.
//
// sub_85CAB0 is luaH_newkey: it places a new key into a table's hash
// part. When the key's main position is occupied by a node that lives
// outside its own main position, it relocates the colliding node:
//
//   othern = mainposition(t, key_of(mp));
//   while (gnext(othern) != mp)        // 0x85CB37..0x85CB46
//       othern = gnext(othern);        //   <-- faults here when
//   gnext(othern) = freenode;          //       othern walks to NULL
//
// If the table's hash chains are already desynced (a node sits at a
// position inconsistent with its key's hash), mp is unreachable from
// othern's chain, so the walk follows next pointers until it hits NULL
// and dereferences (NULL + 0x20) -> crash. This is engine-internal
// corruption; once a chain is broken the original cannot recover.
//
// We cannot un-corrupt the table, but we can stop it from taking down
// the whole process. The fault is a READ at the very first comparison
// of the relocation loop, before newkey writes anything, so catching it
// leaves the table exactly as it already was. We return a private,
// throw-away node: the caller (luaH_set) only writes the *value* into
// the returned node's value slot, so the write lands somewhere harmless
// and the key is simply not inserted. The game keeps running with at
// most a missing table entry instead of a hard crash.
//
// Mirrors lua_gettable_safety.cpp (sub_85BC10): graceful degradation on
// an engine path that would otherwise be fatal.
// ================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"
#include "lua_newkey_safety.h"

extern "C" void Log(const char* fmt, ...);

// int __cdecl sub_85CAB0(lua_State* L, Table* t, TValue* key) -> Node*
typedef void* (__cdecl* newkey_fn)(int L, int t, void* key);
static newkey_fn g_orig_newkey = nullptr;

// Private throw-away node returned when the original would crash. 40 bytes =
// one Node (10 dwords); 16-byte aligned so the caller's TValue store is happy.
// The caller writes only the value slot (first 16 bytes); nothing reads it back.
__declspec(align(16)) static uint8_t g_scratch_node[40] = {};

static volatile LONG64 g_total_calls = 0;
static volatile LONG64 g_recovered   = 0;

static void* __cdecl Safe_newkey(int L, int t, void* key)
{
    ++g_total_calls;
    __try {
        return g_orig_newkey(L, t, key);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Corrupted hash chain — hand back a harmless node instead of crashing.
        ++g_recovered;
        return g_scratch_node;
    }
}

bool InstallLuaNewKeySafety()
{
#if TEST_DISABLE_LUA_NEWKEY_SAFETY
    Log("[NewKeySafety] DISABLED via feature flag");
    return false;
#else
    void* target = (void*)0x0085CAB0;

    // Verify prologue: push ebp; mov ebp, esp
    unsigned char* p = (unsigned char*)target;
    if (p[0] != 0x55 || p[1] != 0x8B || p[2] != 0xEC) {
        Log("[NewKeySafety] BAD PROLOGUE at 0x%08X (got %02X %02X %02X)",
            (uintptr_t)target, p[0], p[1], p[2]);
        return false;
    }

    if (WineSafe_CreateHook(target, (void*)Safe_newkey, (void**)&g_orig_newkey) != MH_OK) {
        Log("[NewKeySafety] MH_CreateHook FAILED");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("[NewKeySafety] MH_EnableHook FAILED");
        MH_RemoveHook(target);
        return false;
    }

    CrashDumper::RegisterFeature("LuaNewKeySafety");
    CrashDumper::FeatureSetActive("LuaNewKeySafety", true);

    Log("[NewKeySafety] ACTIVE: SEH guard on luaH_newkey (sub_85CAB0), fixes 0x85CB43 crash");
    return true;
#endif
}

void UninstallLuaNewKeySafety()
{
#if !TEST_DISABLE_LUA_NEWKEY_SAFETY
    MH_DisableHook((void*)0x0085CAB0);
    MH_RemoveHook((void*)0x0085CAB0);

    LONG64 total = g_total_calls;
    LONG64 recovered = g_recovered;
    if (recovered > 0) {
        Log("[NewKeySafety] Stats: %lld calls | %lld recovered from chain corruption",
            total, recovered);
    }
    CrashDumper::FeatureSetActive("LuaNewKeySafety", false);
#endif
}
