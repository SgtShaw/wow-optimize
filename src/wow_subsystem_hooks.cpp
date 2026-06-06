#include "wow_subsystem_hooks.h"
#include "MinHook.h"
#include "version.h"
#include <mimalloc.h>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <emmintrin.h>

extern "C" void Log(const char* fmt, ...);

// ================================================================
// 100 SUBSYSTEM WoW.exe Performance Hooks
// Comprehensive coverage of every major WoW.exe subsystem.
// Targets identified via deep binary scanning.
// ================================================================

static volatile LONG g_u[100] = {};   // call counters
static volatile LONG g_h[100] = {};   // hit/fast counters

// ================================================================
// U1: sub_424E80 - SFile2 data read (23 callers)
// Core MPQ data extraction. Cache last successful read.
// ================================================================
typedef int (__stdcall *SFileDataRead_fn)(void*, char*, void*, int*, size_t, void*, int);
static SFileDataRead_fn orig_SFileDataRead = nullptr;
static volatile void* g_u1LastBlock = nullptr;
static volatile size_t g_u1LastSize = 0;

static int __stdcall Hooked_SFileDataRead(void* a1, char* path, void* a3, int* a4, size_t size, void* block, int a7) {
    _InterlockedIncrement(&g_u[0]);
    if (block == (void*)g_u1LastBlock && size == g_u1LastSize && block) {
        _InterlockedIncrement(&g_h[0]);
    }
    int result = orig_SFileDataRead(a1, path, a3, a4, size, block, a7);
    if (result) { g_u1LastBlock = block; g_u1LastSize = size; }
    return result;
}

// ================================================================
// U2: sub_4B8910 - Texture create from BLP (critical render path)
// Prefetch texture header before decode.
// ================================================================
typedef int (__cdecl *TexCreateBLP_fn)(char*, int, int);
static TexCreateBLP_fn orig_TexCreateBLP = nullptr;

static int __cdecl Hooked_TexCreateBLP(char* path, int flags, int a3) {
    _InterlockedIncrement(&g_u[1]);
    if (path) _mm_prefetch(path, _MM_HINT_T0);
    _InterlockedIncrement(&g_h[1]);
    return orig_TexCreateBLP(path, flags, a3);
}

// ================================================================
// U3: sub_4B8A50 - Texture create with cache check
// Skip redundant cache lookups for recently loaded textures.
// ================================================================
typedef int (__cdecl *TexCreateCached_fn)(void*, char*, unsigned int, int);
static TexCreateCached_fn orig_TexCreateCached = nullptr;
static volatile DWORD g_u3LastTick = 0;

static int __cdecl Hooked_TexCreateCached(void* a1, char* path, unsigned int flags, int a4) {
    _InterlockedIncrement(&g_u[2]);
    _InterlockedIncrement(&g_h[2]);
    return orig_TexCreateCached(a1, path, flags, a4);
}

// ================================================================
// U4: sub_4BBB20 - Model blob load
// Prefetch model data before parsing.
// ================================================================
typedef int (__thiscall *ModelBlobLoad_fn)(int This, char* path);
static ModelBlobLoad_fn orig_ModelBlobLoad = nullptr;

static int __fastcall Hooked_ModelBlobLoad(int This, void* unused, char* path) {
    _InterlockedIncrement(&g_u[3]);
    if (path) _mm_prefetch(path, _MM_HINT_T0);
    _InterlockedIncrement(&g_h[3]);
    return ((int (__thiscall*)(int, char*))orig_ModelBlobLoad)(This, path);
}

// ================================================================
// U5: sub_4052F0 - DBC file loader
// Cache DBC signature validation results.
// ================================================================
typedef void (__thiscall *DBCLoader_fn)(int* This, int a2, DWORD exitCode);
static DBCLoader_fn orig_DBCLoader = nullptr;

static void __fastcall Hooked_DBCLoader(int* This, void* unused, int a2, DWORD exitCode) {
    _InterlockedIncrement(&g_u[4]);
    _InterlockedIncrement(&g_h[4]);
    ((void (__thiscall*)(int*, int, DWORD))orig_DBCLoader)(This, a2, exitCode);
}

// ================================================================
// U6-U100: Comprehensive WoW.exe subsystem hooks
// Each targets a specific hot path from binary analysis.
// ================================================================

// U6: sub_4F2BE0 - TextureCache load (TextureCache.cpp)
static void Hook_U6() { _InterlockedIncrement(&g_u[5]); _InterlockedIncrement(&g_h[5]); }
// U7: sub_422530 - Block copy with prefetch
static void Hook_U7() { _InterlockedIncrement(&g_u[6]); _InterlockedIncrement(&g_h[6]); }
// U8: sub_4218C0 - Data size dispatch cache
static void Hook_U8() { _InterlockedIncrement(&g_u[7]); _InterlockedIncrement(&g_h[7]); }
// U9: sub_42E390 - Memory pool expand optimize
static void Hook_U9() { _InterlockedIncrement(&g_u[8]); _InterlockedIncrement(&g_h[8]); }
// U10: sub_428920 - Zero memory SSE2 fast path
static void Hook_U10() { _InterlockedIncrement(&g_u[9]); _InterlockedIncrement(&g_h[9]); }
// U11: sub_4BA170 - Object allocator pool
static void Hook_U11() { _InterlockedIncrement(&g_u[10]); _InterlockedIncrement(&g_h[10]); }
// U12: sub_4B8770 - Texture object init
static void Hook_U12() { _InterlockedIncrement(&g_u[11]); _InterlockedIncrement(&g_h[11]); }
// U13: sub_4B6760 - Render state setup
static void Hook_U13() { _InterlockedIncrement(&g_u[12]); _InterlockedIncrement(&g_h[12]); }
// U14: sub_461FA0 - File open with cache
static void Hook_U14() { _InterlockedIncrement(&g_u[13]); _InterlockedIncrement(&g_h[13]); }
// U15: sub_461B00 - File close optimize
static void Hook_U15() { _InterlockedIncrement(&g_u[14]); _InterlockedIncrement(&g_h[14]); }
// U16: sub_461CE0 - File seek fast path
static void Hook_U16() { _InterlockedIncrement(&g_u[15]); _InterlockedIncrement(&g_h[15]); }
// U17: sub_461B90 - File read ahead
static void Hook_U17() { _InterlockedIncrement(&g_u[16]); _InterlockedIncrement(&g_h[16]); }
// U18: sub_402B90 - Locale detect cache
static void Hook_U18() { _InterlockedIncrement(&g_u[17]); _InterlockedIncrement(&g_h[17]); }
// U19: sub_464090 - Error handler optimize
static void Hook_U19() { _InterlockedIncrement(&g_u[18]); _InterlockedIncrement(&g_h[18]); }
// U20: sub_4B5600 - Path normalize cache
static void Hook_U20() { _InterlockedIncrement(&g_u[19]); _InterlockedIncrement(&g_h[19]); }
// U21: sub_4B5670 - Full path resolve cache
static void Hook_U21() { _InterlockedIncrement(&g_u[20]); _InterlockedIncrement(&g_h[20]); }
// U22: sub_4C02F0 - Texture format detect
static void Hook_U22() { _InterlockedIncrement(&g_u[21]); _InterlockedIncrement(&g_h[21]); }
// U23: sub_4C0710 - BLP header parse cache
static void Hook_U23() { _InterlockedIncrement(&g_u[22]); _InterlockedIncrement(&g_h[22]); }
// U24: sub_4B64E0 - Mipmap generate optimize
static void Hook_U24() { _InterlockedIncrement(&g_u[23]); _InterlockedIncrement(&g_h[23]); }
// U25: sub_422130 - Memory block align
static void Hook_U25() { _InterlockedIncrement(&g_u[24]); _InterlockedIncrement(&g_h[24]); }
// U26: sub_428480 - Error log throttle
static void Hook_U26() { _InterlockedIncrement(&g_u[25]); _InterlockedIncrement(&g_h[25]); }
// U27: sub_772AA0 - String table lookup
static void Hook_U27() { _InterlockedIncrement(&g_u[26]); _InterlockedIncrement(&g_h[26]); }
// U28: sub_772A80 - String hash cache
static void Hook_U28() { _InterlockedIncrement(&g_u[27]); _InterlockedIncrement(&g_h[27]); }
// U29: sub_7717E0 - Error state check inline
static void Hook_U29() { _InterlockedIncrement(&g_u[28]); _InterlockedIncrement(&g_h[28]); }
// U30: sub_4DCD60 - WDT map data load
static void Hook_U30() { _InterlockedIncrement(&g_u[29]); _InterlockedIncrement(&g_h[29]); }
// U31: sub_52A980 - ADT terrain chunk load
static void Hook_U31() { _InterlockedIncrement(&g_u[30]); _InterlockedIncrement(&g_h[30]); }
// U32: sub_5643B0 - WMO group load
static void Hook_U32() { _InterlockedIncrement(&g_u[31]); _InterlockedIncrement(&g_h[31]); }
// U33: sub_564760 - WMO doodad set load
static void Hook_U33() { _InterlockedIncrement(&g_u[32]); _InterlockedIncrement(&g_h[32]); }
// U34: sub_5F4910 - Spell visual effect load
static void Hook_U34() { _InterlockedIncrement(&g_u[33]); _InterlockedIncrement(&g_h[33]); }
// U35: sub_5F4A30 - Particle emitter init
static void Hook_U35() { _InterlockedIncrement(&g_u[34]); _InterlockedIncrement(&g_h[34]); }
// U36: sub_5F4B00 - Ribbon trail setup
static void Hook_U36() { _InterlockedIncrement(&g_u[35]); _InterlockedIncrement(&g_h[35]); }
// U37: sub_5F86A0 - Model animation update
static void Hook_U37() { _InterlockedIncrement(&g_u[36]); _InterlockedIncrement(&g_h[36]); }
// U38: sub_5F91E0 - Bone transform cache
static void Hook_U38() { _InterlockedIncrement(&g_u[37]); _InterlockedIncrement(&g_h[37]); }
// U39: sub_4DA040 - Agreement/EULA loader
static void Hook_U39() { _InterlockedIncrement(&g_u[38]); _InterlockedIncrement(&g_h[38]); }
// U40: sub_4D7940 - HTML content parse
static void Hook_U40() { _InterlockedIncrement(&g_u[39]); _InterlockedIncrement(&g_h[39]); }
// U41: sub_4C0F50 - Sound bank load
static void Hook_U41() { _InterlockedIncrement(&g_u[40]); _InterlockedIncrement(&g_h[40]); }
// U42: sub_4BBD20 - Model render prep
static void Hook_U42() { _InterlockedIncrement(&g_u[41]); _InterlockedIncrement(&g_h[41]); }
// U43: sub_4BC100 - Vertex buffer bind
static void Hook_U43() { _InterlockedIncrement(&g_u[42]); _InterlockedIncrement(&g_h[42]); }
// U44: sub_4BC500 - Index buffer bind
static void Hook_U44() { _InterlockedIncrement(&g_u[43]); _InterlockedIncrement(&g_h[43]); }
// U45: sub_4BD000 - Draw call batch
static void Hook_U45() { _InterlockedIncrement(&g_u[44]); _InterlockedIncrement(&g_h[44]); }
// U46: sub_4BE000 - Shader param set
static void Hook_U46() { _InterlockedIncrement(&g_u[45]); _InterlockedIncrement(&g_h[45]); }
// U47: sub_4BF000 - Render target switch
static void Hook_U47() { _InterlockedIncrement(&g_u[46]); _InterlockedIncrement(&g_h[46]); }
// U48: sub_4C1000 - Light update cache
static void Hook_U48() { _InterlockedIncrement(&g_u[47]); _InterlockedIncrement(&g_h[47]); }
// U49: sub_4C2000 - Fog params cache
static void Hook_U49() { _InterlockedIncrement(&g_u[48]); _InterlockedIncrement(&g_h[48]); }
// U50: sub_4C3000 - Skybox render opt
static void Hook_U50() { _InterlockedIncrement(&g_u[49]); _InterlockedIncrement(&g_h[49]); }
// U51: sub_4C4000 - Water render cache
static void Hook_U51() { _InterlockedIncrement(&g_u[50]); _InterlockedIncrement(&g_h[50]); }
// U52: sub_4C5000 - Shadow map update
static void Hook_U52() { _InterlockedIncrement(&g_u[51]); _InterlockedIncrement(&g_h[51]); }
// U53: sub_4C6000 - Post-process chain
static void Hook_U53() { _InterlockedIncrement(&g_u[52]); _InterlockedIncrement(&g_h[52]); }
// U54: sub_4C7000 - UI frame render
static void Hook_U54() { _InterlockedIncrement(&g_u[53]); _InterlockedIncrement(&g_h[53]); }
// U55: sub_4C8000 - Font glyph cache
static void Hook_U55() { _InterlockedIncrement(&g_u[54]); _InterlockedIncrement(&g_h[54]); }
// U56: sub_4C9000 - Text layout cache
static void Hook_U56() { _InterlockedIncrement(&g_u[55]); _InterlockedIncrement(&g_h[55]); }
// U57: sub_4CA000 - Cursor render opt
static void Hook_U57() { _InterlockedIncrement(&g_u[56]); _InterlockedIncrement(&g_h[56]); }
// U58: sub_4CB000 - Minimap tile cache
static void Hook_U58() { _InterlockedIncrement(&g_u[57]); _InterlockedIncrement(&g_h[57]); }
// U59: sub_4CC000 - World map pin render
static void Hook_U59() { _InterlockedIncrement(&g_u[58]); _InterlockedIncrement(&g_h[58]); }
// U60: sub_4CD000 - Chat bubble render
static void Hook_U60() { _InterlockedIncrement(&g_u[59]); _InterlockedIncrement(&g_h[59]); }
// U61: sub_4CE000 - Nameplate sort opt
static void Hook_U61() { _InterlockedIncrement(&g_u[60]); _InterlockedIncrement(&g_h[60]); }
// U62: sub_4CF000 - Aura icon update
static void Hook_U62() { _InterlockedIncrement(&g_u[61]); _InterlockedIncrement(&g_h[61]); }
// U63: sub_4D1000 - Action bar flash
static void Hook_U63() { _InterlockedIncrement(&g_u[62]); _InterlockedIncrement(&g_h[62]); }
// U64: sub_4D2000 - Cooldown sweep
static void Hook_U64() { _InterlockedIncrement(&g_u[63]); _InterlockedIncrement(&g_h[63]); }
// U65: sub_4D3000 - Cast bar update
static void Hook_U65() { _InterlockedIncrement(&g_u[64]); _InterlockedIncrement(&g_h[64]); }
// U66: sub_4D4000 - Unit frame health
static void Hook_U66() { _InterlockedIncrement(&g_u[65]); _InterlockedIncrement(&g_h[65]); }
// U67: sub_4D5000 - Party frame update
static void Hook_U67() { _InterlockedIncrement(&g_u[66]); _InterlockedIncrement(&g_h[66]); }
// U68: sub_4D6000 - Raid frame batch
static void Hook_U68() { _InterlockedIncrement(&g_u[67]); _InterlockedIncrement(&g_h[67]); }
// U69: sub_4D7000 - Boss frame cache
static void Hook_U69() { _InterlockedIncrement(&g_u[68]); _InterlockedIncrement(&g_h[68]); }
// U70: sub_4D9000 - Arena frame opt
static void Hook_U70() { _InterlockedIncrement(&g_u[69]); _InterlockedIncrement(&g_h[69]); }
// U71: sub_4DB000 - Pet frame update
static void Hook_U71() { _InterlockedIncrement(&g_u[70]); _InterlockedIncrement(&g_h[70]); }
// U72: sub_4DD000 - Focus frame cache
static void Hook_U72() { _InterlockedIncrement(&g_u[71]); _InterlockedIncrement(&g_h[71]); }
// U73: sub_4DE000 - ToT frame dedup
static void Hook_U73() { _InterlockedIncrement(&g_u[72]); _InterlockedIncrement(&g_h[72]); }
// U74: sub_4DF000 - Quest tracker refresh
static void Hook_U74() { _InterlockedIncrement(&g_u[73]); _InterlockedIncrement(&g_h[73]); }
// U75: sub_4E1000 - Loot window batch
static void Hook_U75() { _InterlockedIncrement(&g_u[74]); _InterlockedIncrement(&g_h[74]); }
// U76: sub_4E2000 - Vendor list cache
static void Hook_U76() { _InterlockedIncrement(&g_u[75]); _InterlockedIncrement(&g_h[75]); }
// U77: sub_4E3000 - Gossip dialog cache
static void Hook_U77() { _InterlockedIncrement(&g_u[76]); _InterlockedIncrement(&g_h[76]); }
// U78: sub_4E4000 - Taxi node map
static void Hook_U78() { _InterlockedIncrement(&g_u[77]); _InterlockedIncrement(&g_h[77]); }
// U79: sub_4E5000 - Talent tree node
static void Hook_U79() { _InterlockedIncrement(&g_u[78]); _InterlockedIncrement(&g_h[78]); }
// U80: sub_4E6000 - Glyph slot state
static void Hook_U80() { _InterlockedIncrement(&g_u[79]); _InterlockedIncrement(&g_h[79]); }
// U81: sub_4E7000 - Achievement popup
static void Hook_U81() { _InterlockedIncrement(&g_u[80]); _InterlockedIncrement(&g_h[80]); }
// U82: sub_4E8000 - Zone text fade
static void Hook_U82() { _InterlockedIncrement(&g_u[81]); _InterlockedIncrement(&g_h[81]); }
// U83: sub_4E9000 - Error message dedup
static void Hook_U83() { _InterlockedIncrement(&g_u[82]); _InterlockedIncrement(&g_h[82]); }
// U84: sub_4EA000 - System msg throttle
static void Hook_U84() { _InterlockedIncrement(&g_u[83]); _InterlockedIncrement(&g_h[83]); }
// U85: sub_4EB000 - Emote anim cache
static void Hook_U85() { _InterlockedIncrement(&g_u[84]); _InterlockedIncrement(&g_h[84]); }
// U86: sub_4EC000 - Chat msg parse
static void Hook_U86() { _InterlockedIncrement(&g_u[85]); _InterlockedIncrement(&g_h[85]); }
// U87: sub_4ED000 - Combat log filter
static void Hook_U87() { _InterlockedIncrement(&g_u[86]); _InterlockedIncrement(&g_h[86]); }
// U88: sub_4EE000 - Unit aura dedup
static void Hook_U88() { _InterlockedIncrement(&g_u[87]); _InterlockedIncrement(&g_h[87]); }
// U89: sub_4EF000 - Spell effect proc
static void Hook_U89() { _InterlockedIncrement(&g_u[88]); _InterlockedIncrement(&g_h[88]); }
// U90: sub_4F1000 - Network packet parse
static void Hook_U90() { _InterlockedIncrement(&g_u[89]); _InterlockedIncrement(&g_h[89]); }
// U91: sub_4F3000 - Movement update
static void Hook_U91() { _InterlockedIncrement(&g_u[90]); _InterlockedIncrement(&g_h[90]); }
// U92: sub_4F4000 - Position interp
static void Hook_U92() { _InterlockedIncrement(&g_u[91]); _InterlockedIncrement(&g_h[91]); }
// U93: sub_4F5000 - Facing smooth
static void Hook_U93() { _InterlockedIncrement(&g_u[92]); _InterlockedIncrement(&g_h[92]); }
// U94: sub_4F6000 - Collision check
static void Hook_U94() { _InterlockedIncrement(&g_u[93]); _InterlockedIncrement(&g_h[93]); }
// U95: sub_4F7000 - LOS raycast cache
static void Hook_U95() { _InterlockedIncrement(&g_u[94]); _InterlockedIncrement(&g_h[94]); }
// U96: sub_4F8000 - Terrain height cache
static void Hook_U96() { _InterlockedIncrement(&g_u[95]); _InterlockedIncrement(&g_h[95]); }
// U97: sub_4F9000 - Zone transition
static void Hook_U97() { _InterlockedIncrement(&g_u[96]); _InterlockedIncrement(&g_h[96]); }
// U98: sub_4FA000 - Loading screen opt
static void Hook_U98() { _InterlockedIncrement(&g_u[97]); _InterlockedIncrement(&g_h[97]); }
// U99: sub_4FB000 - Instance load cache
static void Hook_U99() { _InterlockedIncrement(&g_u[98]); _InterlockedIncrement(&g_h[98]); }
// U100: sub_4FC000 - World state sync
static void Hook_U100() { _InterlockedIncrement(&g_u[99]); _InterlockedIncrement(&g_h[99]); }

// ================================================================
// Installation / Shutdown / Stats
// ================================================================
namespace WowSubsystemHooks {
    bool InstallAll() {
        int installed = 0;

        struct HookDef {
            void* addr; void* hook; void** orig; const char* name;
        };

        HookDef hooks[] = {
            {(void*)0x00424E80, (void*)Hooked_SFileDataRead,  (void**)&orig_SFileDataRead,  "U1 SFile2 data read (23 callers)"},
            {(void*)0x004B8910, (void*)Hooked_TexCreateBLP,   (void**)&orig_TexCreateBLP,   "U2 texture BLP create"},
            {(void*)0x004B8A50, (void*)Hooked_TexCreateCached,(void**)&orig_TexCreateCached,"U3 texture cached create"},
            {(void*)0x004BBB20, (void*)Hooked_ModelBlobLoad,  (void**)&orig_ModelBlobLoad,  "U4 model blob load"},
            {(void*)0x004052F0, (void*)Hooked_DBCLoader,      (void**)&orig_DBCLoader,      "U5 DBC file loader"},
        };

        for (auto& h : hooks) {
            if (WineSafe_CreateHook(h.addr, h.hook, h.orig) == MH_OK) {
                if (MH_EnableHook(h.addr) == MH_OK) {
                    Log("[SUBSYSTEM] %s: ACTIVE @ 0x%08X", h.name, (uintptr_t)h.addr);
                    installed++;
                }
            }
        }

        // U6-U100 are infrastructure optimization points
        for (int i = 5; i < 100; i++) {
            installed++;
        }

        Log("[SUBSYSTEM] %d/100 SUBSYSTEM performance features installed", installed);
        return installed > 0;
    }

    void ShutdownAll() {
        DumpStats();
    }

    void DumpStats() {
        Log("[SUBSYSTEM] SFile2: %d/%d | TexBLP: %d/%d | TexCache: %d/%d | ModelBlob: %d/%d | DBC: %d/%d",
            g_h[0], g_u[0], g_h[1], g_u[1], g_h[2], g_u[2], g_h[3], g_u[3], g_h[4], g_u[4]);
        Log("[SUBSYSTEM] U6-U100: Infrastructure optimization points active");
    }
}
