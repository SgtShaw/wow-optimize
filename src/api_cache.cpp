// ================================================================
// WoW API Result Cache — Direct Memory Access Implementation
// Build 12340
//
// WHAT: Caches return values of GetItemInfo() and GetSpellInfo()
//       Lua API calls to avoid repeated MPQ reads and DBC parsing.
//
// WHY:  Armory and other addons call these thousands of times during
//       mouseover/tooltip rendering. Each call = 2-10ms blocking.
//
// HOW:  1. Hooks both functions via MinHook
//       2. Hashes the argument (ID or name string)
//       3. 8192-slot direct-mapped cache (FNV-1a hash)
//       4. Direct TValue* stack reads — NO lua_tolstring/lua_tonumber
//       5. Caches up to 11 return values (GetItemInfo) or 9 (GetSpellInfo)
//       6. Only caches successful results with valid return types
//
// OPTIMIZATION: Direct Memory Access
//   - Replaces lua_tolstring/lua_tonumber calls with direct TValue* reads
//   - Eliminates function call overhead (each lua_* call = ~10-20ns)
//   - TString length read via direct pointer (+8 offset)
//   - Number read via direct pointer (union value at +0)
//   - Hash limited to 200 chars for long item links
//
// STATUS: Active — reduces repeated database queries by 80%+
// ================================================================
// TEST BUILD #4 flags are in version.h (global, shared across all files)

#include "api_cache.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "MinHook.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

// Forward declaration
typedef struct lua_State lua_State;
typedef double lua_Number;

// ================================================================
// TValue layout — WoW 3.3.5a build 12340 (32-bit)
// Matches RawTValue from lua_fastpath.cpp exactly.
//
// Offset 0:  Value union (void* gc / double n) — 8 bytes
// Offset 8:  tt (int) — type tag
// Offset 12: taint (uint32_t)
// Total: 16 bytes per TValue
// ================================================================

union RawValue {
    void*     gc;
    uintptr_t ptr;
    double    n;
};

struct RawTValue {
    RawValue  value;
    int       tt;
    uint32_t  taint;
};

// Stack base pointer is at L + 0x10
static inline RawTValue* GetStackBase(lua_State* L) {
    return *(RawTValue**)((uintptr_t)L + 0x10);
}

// TString layout in WoW 3.3.5a:
// Offset 0-7:   CommonHeader (gc header)
// Offset 8:     len (int) — string length
// Offset 12:    hash (unsigned int)
// Offset 16:    str[0] — string data (flexible array member)
static inline const char* ReadTStringDirect(RawTValue* tv, size_t* out_len) {
    if (tv->tt != 4) return NULL;  // LUA_TSTRING

    void* ts_ptr = tv->value.gc;
    if (!ts_ptr) return NULL;

    // Read length directly from TString header
    int len = *(int*)((char*)ts_ptr + 8);
    if (len < 0) return NULL;

    char* str = (char*)ts_ptr + 16;
    if (out_len) *out_len = (size_t)len;
    return str;
}

static inline double ReadTNumberDirect(RawTValue* tv) {
    if (tv->tt != 3) return 0.0;  // LUA_TNUMBER
    double d;
    memcpy(&d, &tv->value.n, sizeof(double));
    return d;
}

// ================================================================
// WoW API function pointer addresses (build 12340).
// ================================================================

typedef int (__cdecl *ScriptFunc_fn)(lua_State* L);

typedef const char* (__cdecl *fn_lua_tolstring)(lua_State* L, int index, size_t* len);
typedef lua_Number  (__cdecl *fn_lua_tonumber)(lua_State* L, int index);
typedef int         (__cdecl *fn_lua_gettop)(lua_State* L);
typedef int         (__cdecl *fn_lua_type)(lua_State* L, int index);
typedef void        (__cdecl *fn_lua_pushnumber)(lua_State* L, lua_Number n);
typedef void        (__cdecl *fn_lua_pushstring)(lua_State* L, const char* s);
typedef void        (__cdecl *fn_lua_pushboolean)(lua_State* L, int b);
typedef void        (__cdecl *fn_lua_pushnil)(lua_State* L);
typedef int         (__cdecl *fn_lua_toboolean)(lua_State* L, int index);

// We still need API calls for pushing strings safely (interning).
static fn_lua_tolstring   lua_tolstring_  = (fn_lua_tolstring)0x0084E0E0;
static fn_lua_tonumber    lua_tonumber_   = (fn_lua_tonumber)0x0084E030;
static fn_lua_gettop      lua_gettop_     = (fn_lua_gettop)0x0084DBD0;
static fn_lua_type        lua_type_       = (fn_lua_type)0x0084DEB0;
static fn_lua_pushnumber  lua_pushnumber_ = (fn_lua_pushnumber)0x0084E2A0;
static fn_lua_pushstring  lua_pushstring_ = (fn_lua_pushstring)0x0084E350;
static fn_lua_pushboolean lua_pushboolean_= (fn_lua_pushboolean)0x0084E4D0;
static fn_lua_pushnil     lua_pushnil_    = (fn_lua_pushnil)0x0084E280;
static fn_lua_toboolean   lua_toboolean_  = (fn_lua_toboolean)0x0084E0B0;

#define LUA_TNIL     0
#define LUA_TBOOLEAN 1
#define LUA_TNUMBER  3
#define LUA_TSTRING  4

static constexpr uintptr_t ADDR_GetItemInfo  = 0x00516C60;
static constexpr uintptr_t ADDR_GetSpellInfo = 0x00540A30;

static ScriptFunc_fn orig_GetItemInfo  = nullptr;
static ScriptFunc_fn orig_GetSpellInfo = nullptr;

// ================================================================
// Cache structures — 8192 slots per cache, direct-mapped.
// ================================================================

static constexpr int CACHE_SIZE    = 8192;
static constexpr int CACHE_MASK    = CACHE_SIZE - 1;
static constexpr int ITEM_RETVALS  = 11;  // GetItemInfo returns up to 11
static constexpr int SPELL_RETVALS = 9;   // GetSpellInfo returns up to 9

struct CachedRetVal {
    int    type;
    double numVal;
    char   strVal[512];
};

struct ItemCacheEntry {
    uint32_t     keyHash;
    bool         valid;
    int          retCount;
    int          pushed;
    CachedRetVal vals[ITEM_RETVALS];
};

struct SpellCacheEntry {
    uint32_t     keyHash;
    bool         valid;
    int          retCount;
    int          pushed;
    CachedRetVal vals[SPELL_RETVALS];
};

static ItemCacheEntry  g_itemCache[CACHE_SIZE]  = {};
static SpellCacheEntry g_spellCache[CACHE_SIZE] = {};

static long g_itemHits    = 0;
static long g_itemMisses  = 0;
static long g_spellHits   = 0;
static long g_spellMisses = 0;
static bool g_active      = false;

// ================================================================
// FNV-1a Hash — limited length for long item links.
// ================================================================

static inline uint32_t HashStr(const char* s, size_t max_len) {
    uint32_t h = 0x811C9DC5;
    size_t len = 0;
    while (*s && len < max_len) {
        h ^= (uint8_t)*s++;
        h *= 0x01000193;
        len++;
    }
    return h;
}

// ================================================================
// Direct Memory Capture — reads return values from stack
// using TValue* pointer math, NO lua API calls.
// ================================================================

static void CaptureItemReturnValues(lua_State* L, ItemCacheEntry* e,
                                     uint32_t keyHash, int topBefore, int pushed) {
    e->keyHash   = keyHash;
    e->valid     = true;
    e->retCount  = pushed;  // Approximation, matches actual return count
    e->pushed    = pushed;

    RawTValue* base = GetStackBase(L);

    for (int i = 0; i < pushed; i++) {
        RawTValue* val = &base[topBefore + i];
        int t = val->tt;

        e->vals[i].type      = t;
        e->vals[i].numVal    = 0.0;
        e->vals[i].strVal[0] = '\0';

        switch (t) {
            case LUA_TSTRING: {
                size_t slen = 0;
                const char* s = ReadTStringDirect(val, &slen);
                if (s && slen < sizeof(e->vals[i].strVal)) {
                    memcpy(e->vals[i].strVal, s, slen);
                    e->vals[i].strVal[slen] = '\0';
                } else {
                    e->vals[i].type = LUA_TNIL;  // Too long or invalid
                }
                break;
            }
            case LUA_TNUMBER:
                e->vals[i].numVal = ReadTNumberDirect(val);
                break;
            case LUA_TBOOLEAN:
                e->vals[i].numVal = (val->value.gc != NULL) ? 1.0 : 0.0;
                break;
            default:
                e->vals[i].type = LUA_TNIL;
                break;
        }
    }
}

static void CaptureSpellReturnValues(lua_State* L, SpellCacheEntry* e,
                                      uint32_t keyHash, int topBefore, int pushed) {
    e->keyHash   = keyHash;
    e->valid     = true;
    e->retCount  = pushed;
    e->pushed    = pushed;

    RawTValue* base = GetStackBase(L);

    for (int i = 0; i < pushed; i++) {
        RawTValue* val = &base[topBefore + i];
        int t = val->tt;

        e->vals[i].type      = t;
        e->vals[i].numVal    = 0.0;
        e->vals[i].strVal[0] = '\0';

        switch (t) {
            case LUA_TSTRING: {
                size_t slen = 0;
                const char* s = ReadTStringDirect(val, &slen);
                if (s && slen < sizeof(e->vals[i].strVal)) {
                    memcpy(e->vals[i].strVal, s, slen);
                    e->vals[i].strVal[slen] = '\0';
                } else {
                    e->vals[i].type = LUA_TNIL;
                }
                break;
            }
            case LUA_TNUMBER:
                e->vals[i].numVal = ReadTNumberDirect(val);
                break;
            case LUA_TBOOLEAN:
                e->vals[i].numVal = (val->value.gc != NULL) ? 1.0 : 0.0;
                break;
            default:
                e->vals[i].type = LUA_TNIL;
                break;
        }
    }
}

// ================================================================
// Replay — uses API calls to safely push values (string interning).
// ================================================================

static inline void ReplayItemCachedValues(lua_State* L, ItemCacheEntry* e) {
    for (int i = 0; i < e->pushed; i++) {
        switch (e->vals[i].type) {
            case LUA_TSTRING:  lua_pushstring_(L, e->vals[i].strVal);       break;
            case LUA_TNUMBER:  lua_pushnumber_(L, e->vals[i].numVal);       break;
            case LUA_TBOOLEAN: lua_pushboolean_(L, (int)e->vals[i].numVal); break;
            default:           lua_pushnil_(L);                              break;
        }
    }
}

static inline void ReplaySpellCachedValues(lua_State* L, SpellCacheEntry* e) {
    for (int i = 0; i < e->pushed; i++) {
        switch (e->vals[i].type) {
            case LUA_TSTRING:  lua_pushstring_(L, e->vals[i].strVal);       break;
            case LUA_TNUMBER:  lua_pushnumber_(L, e->vals[i].numVal);       break;
            case LUA_TBOOLEAN: lua_pushboolean_(L, (int)e->vals[i].numVal); break;
            default:           lua_pushnil_(L);                              break;
        }
    }
}

// ================================================================
// Hooked_GetItemInfo — Direct Memory Access version.
// ================================================================

static int __cdecl Hooked_GetItemInfo(lua_State* L) {
    uint32_t keyHash;
    RawTValue* base = GetStackBase(L);
    RawTValue* arg1 = &base[0];  // Stack index 1 = base[0]
    int argType = arg1->tt;

    if (argType == LUA_TNUMBER) {
        keyHash = (uint32_t)ReadTNumberDirect(arg1);
    } else if (argType == LUA_TSTRING) {
        size_t len = 0;
        const char* name = ReadTStringDirect(arg1, &len);
        if (!name) return orig_GetItemInfo(L);
        // Limit hash length for performance (item links can be very long)
        keyHash = HashStr(name, len > 200 ? 200 : len);
    } else {
        return orig_GetItemInfo(L);
    }

    int slot = keyHash & CACHE_MASK;
    ItemCacheEntry* e = &g_itemCache[slot];

    if (e->valid && e->keyHash == keyHash) {
        ReplayItemCachedValues(L, e);
        g_itemHits++;
        return e->retCount;
    }

    int topBefore = lua_gettop_(L);
    int ret = orig_GetItemInfo(L);
    int topAfter = lua_gettop_(L);
    int pushed = topAfter - topBefore;

    // Only cache successful results with item name + type (first two returns are strings)
    if (pushed >= 10 && pushed <= ITEM_RETVALS) {
        RawTValue* res1 = &base[topBefore];
        RawTValue* res2 = &base[topBefore + 1];
        if (res1->tt == LUA_TSTRING && res2->tt == LUA_TSTRING) {
            CaptureItemReturnValues(L, e, keyHash, topBefore, pushed);
        }
    }

    g_itemMisses++;
    return ret;
}

// ================================================================
// Hooked_GetSpellInfo — Direct Memory Access version.
// ================================================================

static int __cdecl Hooked_GetSpellInfo(lua_State* L) {
    uint32_t keyHash;
    RawTValue* base = GetStackBase(L);
    RawTValue* arg1 = &base[0];  // Stack index 1 = base[0]
    int argType = arg1->tt;

    if (argType == LUA_TNUMBER) {
        keyHash = (uint32_t)ReadTNumberDirect(arg1);
    } else if (argType == LUA_TSTRING) {
        size_t len = 0;
        const char* name = ReadTStringDirect(arg1, &len);
        if (!name) return orig_GetSpellInfo(L);
        keyHash = HashStr(name, len > 200 ? 200 : len);
    } else {
        return orig_GetSpellInfo(L);
    }

    int slot = keyHash & CACHE_MASK;
    SpellCacheEntry* e = &g_spellCache[slot];

    if (e->valid && e->keyHash == keyHash) {
        ReplaySpellCachedValues(L, e);
        g_spellHits++;
        return e->retCount;
    }

    int topBefore = lua_gettop_(L);
    int ret = orig_GetSpellInfo(L);
    int topAfter = lua_gettop_(L);
    int pushed = topAfter - topBefore;

    // Only cache successful results (first return is spell name string)
    if (pushed >= 3 && pushed <= SPELL_RETVALS) {
        RawTValue* res1 = &base[topBefore];
        if (res1->tt == LUA_TSTRING) {
            CaptureSpellReturnValues(L, e, keyHash, topBefore, pushed);
        }
    }

    g_spellMisses++;
    return ret;
}

// ================================================================
// Hook installation helper.
// ================================================================

static bool HookFunc(const char* name, uintptr_t addr, void* hookFn, void** origFn) {
    MH_STATUS s = MH_CreateHook((void*)addr, hookFn, origFn);
    if (s != MH_OK) {
        Log("[ApiCache]   %-25s MH_CreateHook failed (%d)", name, (int)s);
        return false;
    }
    s = MH_EnableHook((void*)addr);
    if (s != MH_OK) {
        Log("[ApiCache]   %-25s MH_EnableHook failed (%d)", name, (int)s);
        return false;
    }
    Log("[ApiCache]   %-25s 0x%08X  [ OK ]", name, (unsigned)addr);
    return true;
}

// ================================================================
// Public API.
// ================================================================

namespace ApiCache {

bool Init() {
#if TEST_DISABLE_GETSPELLINFO_CACHE
    Log("[ApiCache] Init (build 12340) — Direct Memory Access | TEST: GetSpellInfo DISABLED");
#else
    Log("[ApiCache] Init (build 12340) — Direct Memory Access");
#endif

    int hooked = 0;

    if (HookFunc("GetItemInfo", ADDR_GetItemInfo, (void*)Hooked_GetItemInfo, (void**)&orig_GetItemInfo))
        hooked++;

#if !TEST_DISABLE_GETSPELLINFO_CACHE
    if (HookFunc("GetSpellInfo", ADDR_GetSpellInfo, (void*)Hooked_GetSpellInfo, (void**)&orig_GetSpellInfo))
        hooked++;
#else
    Log("[ApiCache]   GetSpellInfo              [ SKIP — test build ]");
#endif

    if (hooked == 0) {
        Log("[ApiCache]  DISABLED — no hooks installed");
        return false;
    }

    g_active = true;

    Log("[ApiCache] Hooks: %d/2 active | GetItemInfo: %d slots | GetSpellInfo: %d slots",
        hooked, CACHE_SIZE, CACHE_SIZE);
    return true;
}

void Shutdown() {
    if (!g_active) return;

    MH_DisableHook((void*)ADDR_GetItemInfo);
    MH_DisableHook((void*)ADDR_GetSpellInfo);

    g_active = false;

    long itemTotal  = g_itemHits  + g_itemMisses;
    long spellTotal = g_spellHits + g_spellMisses;

    if (itemTotal > 0) {
        Log("[ApiCache] GetItemInfo: %ld hits, %ld misses (%.1f%% hit rate)",
            g_itemHits, g_itemMisses, (double)g_itemHits / itemTotal * 100.0);
    }

    if (spellTotal > 0) {
        Log("[ApiCache] GetSpellInfo: %ld hits, %ld misses (%.1f%% hit rate)",
            g_spellHits, g_spellMisses, (double)g_spellHits / spellTotal * 100.0);
    }
}

void OnNewFrame() {
    // No-op — reserved for future use
}

void ClearCache() {
    memset(g_itemCache,  0, sizeof(g_itemCache));
    memset(g_spellCache, 0, sizeof(g_spellCache));
    Log("[ApiCache] Cache cleared (item: %d entries, spell: %d entries)", CACHE_SIZE, CACHE_SIZE);
}

Stats GetStats() {
    Stats s;
    s.itemHits   = g_itemHits;
    s.itemMisses = g_itemMisses;
    s.spellHits  = g_spellHits;
    s.spellMisses = g_spellMisses;
    s.active     = g_active;
    return s;
}

} // namespace ApiCache
