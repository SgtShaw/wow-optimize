#include "config.h"
#include <windows.h>
#include <string>

namespace Config {
    Settings g_settings;

    void Load() {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        std::string iniPath = path;
        size_t lastSlash = iniPath.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            iniPath = iniPath.substr(0, lastSlash + 1) + "wow_opt.ini";
        } else {
            iniPath = "wow_opt.ini";
        }

        // Check if the file exists, if not, write the default template
        DWORD attribs = GetFileAttributesA(iniPath.c_str());
        if (attribs == INVALID_FILE_ATTRIBUTES) {
            // Write default ini template with safe defaults (mostly 0 except core/stable guards)
            // General
            WritePrivateProfileStringA("General", "SleepPrecision", "1", iniPath.c_str());
            WritePrivateProfileStringA("General", "SleepPrecisionValue", "8", iniPath.c_str());
            WritePrivateProfileStringA("General", "MemoryPressure", "1", iniPath.c_str());
            WritePrivateProfileStringA("General", "HeapCompactor", "1", iniPath.c_str());
            WritePrivateProfileStringA("General", "DefragLf", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "Allocators", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "VulkanDXVK", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "TimingFix", "0", iniPath.c_str());
            WritePrivateProfileStringA("General", "CvarNullGuard", "1", iniPath.c_str());
            WritePrivateProfileStringA("General", "FrameLimiter", "0", iniPath.c_str());

            // UI & Lua
            WritePrivateProfileStringA("UI_Lua", "UIFrameBatch", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "AddonDispatcher", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "UIFrameAccessorFast", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "FontMetricsFast", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "FontMetricsLockFree", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "FrameXmlCoalesce", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "AddonTickGovernor", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "TooltipCache", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "LuaFileCache", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "FrameScriptDispatch", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "LuaNumConvFast", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "LuaOpcache", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "LuaGcCoalesce", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "LuaJIT", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "AsyncTexLoader", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "AsyncTerrainLoader", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "RcuObjMgr", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "M2LodBias", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "MipBiasGovernor", "0", iniPath.c_str());
            WritePrivateProfileStringA("UI_Lua", "SpatialCulling", "0", iniPath.c_str());

            // Combat & Network
            WritePrivateProfileStringA("Combat_Net", "CombatLogParser", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "CombatLogIncremental", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "EventCoalescer", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "NetPacketCoalesce", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "UnitAuraCoalesce", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "NetAddonCoalescer", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "SavedVarsSerializer", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "SavedVarsAsync", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "SavedVarsPretoken", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "UnitAuraFast", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "NetworkGuidSse2", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "GetSpellInfoCache", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "PacketOffload", "0", iniPath.c_str());
            WritePrivateProfileStringA("Combat_Net", "NameplateMT", "0", iniPath.c_str());

            // Graphics & Sound
            WritePrivateProfileStringA("Graphics_Sound", "StrStrSse2", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "StrCatFast", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "SoundMixerOpt", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "AudioDecodeMt", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "DbcLookupCache", "0", iniPath.c_str());
            WritePrivateProfileStringA("Graphics_Sound", "WorldStateCoalesce", "0", iniPath.c_str());
        }

        // Read all settings
        // General
        g_settings.OptSleepPrecision      = GetPrivateProfileIntA("General", "SleepPrecision", 1, iniPath.c_str()) != 0;
        g_settings.SleepPrecisionValue    = GetPrivateProfileIntA("General", "SleepPrecisionValue", 8, iniPath.c_str());
        g_settings.OptMemoryPressure      = GetPrivateProfileIntA("General", "MemoryPressure", 1, iniPath.c_str()) != 0;
        g_settings.OptHeapCompactor       = GetPrivateProfileIntA("General", "HeapCompactor", 1, iniPath.c_str()) != 0;
        g_settings.OptDefragLf            = GetPrivateProfileIntA("General", "DefragLf", 0, iniPath.c_str()) != 0;
        g_settings.OptAllocators          = GetPrivateProfileIntA("General", "Allocators", 0, iniPath.c_str()) != 0;
        g_settings.OptVulkanDXVK          = GetPrivateProfileIntA("General", "VulkanDXVK", 0, iniPath.c_str()) != 0;
        g_settings.OptTimingFix           = GetPrivateProfileIntA("General", "TimingFix", 0, iniPath.c_str()) != 0;
        g_settings.OptCvarNullGuard       = GetPrivateProfileIntA("General", "CvarNullGuard", 1, iniPath.c_str()) != 0;
        g_settings.OptFrameLimiter        = GetPrivateProfileIntA("General", "FrameLimiter", 0, iniPath.c_str()) != 0;

        // UI & Lua
        g_settings.OptUIFrameBatch        = GetPrivateProfileIntA("UI_Lua", "UIFrameBatch", 0, iniPath.c_str()) != 0;
        g_settings.OptAddonDispatcher     = GetPrivateProfileIntA("UI_Lua", "AddonDispatcher", 0, iniPath.c_str()) != 0;
        g_settings.OptUIFrameAccessorFast = GetPrivateProfileIntA("UI_Lua", "UIFrameAccessorFast", 0, iniPath.c_str()) != 0;
        g_settings.OptFontMetricsFast     = GetPrivateProfileIntA("UI_Lua", "FontMetricsFast", 0, iniPath.c_str()) != 0;
        g_settings.OptFontMetricsLockFree = GetPrivateProfileIntA("UI_Lua", "FontMetricsLockFree", 0, iniPath.c_str()) != 0;
        g_settings.OptFrameXmlCoalesce    = GetPrivateProfileIntA("UI_Lua", "FrameXmlCoalesce", 0, iniPath.c_str()) != 0;
        g_settings.OptAddonTickGovernor   = GetPrivateProfileIntA("UI_Lua", "AddonTickGovernor", 0, iniPath.c_str()) != 0;
        g_settings.OptTooltipCache        = GetPrivateProfileIntA("UI_Lua", "TooltipCache", 0, iniPath.c_str()) != 0;
        g_settings.OptLuaFileCache        = GetPrivateProfileIntA("UI_Lua", "LuaFileCache", 0, iniPath.c_str()) != 0;
        g_settings.OptFrameScriptDispatch = GetPrivateProfileIntA("UI_Lua", "FrameScriptDispatch", 0, iniPath.c_str()) != 0;
        g_settings.OptLuaNumConvFast      = GetPrivateProfileIntA("UI_Lua", "LuaNumConvFast", 0, iniPath.c_str()) != 0;
        g_settings.OptLuaOpcache          = GetPrivateProfileIntA("UI_Lua", "LuaOpcache", 0, iniPath.c_str()) != 0;
        g_settings.OptLuaGcCoalesce       = GetPrivateProfileIntA("UI_Lua", "LuaGcCoalesce", 0, iniPath.c_str()) != 0;
        g_settings.OptLuaJIT              = GetPrivateProfileIntA("UI_Lua", "LuaJIT", 0, iniPath.c_str()) != 0;
        g_settings.OptAsyncTexLoader      = GetPrivateProfileIntA("UI_Lua", "AsyncTexLoader", 0, iniPath.c_str()) != 0;
        g_settings.OptAsyncTerrainLoader  = GetPrivateProfileIntA("UI_Lua", "AsyncTerrainLoader", 0, iniPath.c_str()) != 0;
        g_settings.OptRcuObjMgr           = GetPrivateProfileIntA("UI_Lua", "RcuObjMgr", 0, iniPath.c_str()) != 0;
        g_settings.OptM2LodBias           = GetPrivateProfileIntA("UI_Lua", "M2LodBias", 0, iniPath.c_str()) != 0;
        g_settings.OptMipBiasGovernor     = GetPrivateProfileIntA("UI_Lua", "MipBiasGovernor", 0, iniPath.c_str()) != 0;
        g_settings.OptSpatialCulling      = GetPrivateProfileIntA("UI_Lua", "SpatialCulling", 0, iniPath.c_str()) != 0;

        // Combat & Network
        g_settings.OptCombatLogParser     = GetPrivateProfileIntA("Combat_Net", "CombatLogParser", 0, iniPath.c_str()) != 0;
        g_settings.OptCombatLogIncremental = GetPrivateProfileIntA("Combat_Net", "CombatLogIncremental", 0, iniPath.c_str()) != 0;
        g_settings.OptEventCoalescer      = GetPrivateProfileIntA("Combat_Net", "EventCoalescer", 0, iniPath.c_str()) != 0;
        g_settings.OptNetPacketCoalesce   = GetPrivateProfileIntA("Combat_Net", "NetPacketCoalesce", 0, iniPath.c_str()) != 0;
        g_settings.OptUnitAuraCoalesce    = GetPrivateProfileIntA("Combat_Net", "UnitAuraCoalesce", 0, iniPath.c_str()) != 0;
        g_settings.OptNetAddonCoalescer   = GetPrivateProfileIntA("Combat_Net", "NetAddonCoalescer", 0, iniPath.c_str()) != 0;
        g_settings.OptSavedVarsSerializer = GetPrivateProfileIntA("Combat_Net", "SavedVarsSerializer", 0, iniPath.c_str()) != 0;
        g_settings.OptSavedVarsAsync      = GetPrivateProfileIntA("Combat_Net", "SavedVarsAsync", 0, iniPath.c_str()) != 0;
        g_settings.OptSavedVarsPretoken   = GetPrivateProfileIntA("Combat_Net", "SavedVarsPretoken", 0, iniPath.c_str()) != 0;
        g_settings.OptUnitAuraFast        = GetPrivateProfileIntA("Combat_Net", "UnitAuraFast", 0, iniPath.c_str()) != 0;
        g_settings.OptNetworkGuidSse2     = GetPrivateProfileIntA("Combat_Net", "NetworkGuidSse2", 0, iniPath.c_str()) != 0;
        g_settings.OptGetSpellInfoCache   = GetPrivateProfileIntA("Combat_Net", "GetSpellInfoCache", 0, iniPath.c_str()) != 0;
        g_settings.OptPacketOffload       = GetPrivateProfileIntA("Combat_Net", "PacketOffload", 0, iniPath.c_str()) != 0;
        g_settings.OptNameplateMT         = GetPrivateProfileIntA("Combat_Net", "NameplateMT", 0, iniPath.c_str()) != 0;

        // Graphics & Sound
        g_settings.OptStrStrSse2          = GetPrivateProfileIntA("Graphics_Sound", "StrStrSse2", 0, iniPath.c_str()) != 0;
        g_settings.OptStrCatFast          = GetPrivateProfileIntA("Graphics_Sound", "StrCatFast", 0, iniPath.c_str()) != 0;
        g_settings.OptSoundMixerOpt       = GetPrivateProfileIntA("Graphics_Sound", "SoundMixerOpt", 0, iniPath.c_str()) != 0;
        g_settings.OptAudioDecodeMt       = GetPrivateProfileIntA("Graphics_Sound", "AudioDecodeMt", 0, iniPath.c_str()) != 0;
        g_settings.OptDbcLookupCache      = GetPrivateProfileIntA("Graphics_Sound", "DbcLookupCache", 0, iniPath.c_str()) != 0;
        g_settings.OptWorldStateCoalesce  = GetPrivateProfileIntA("Graphics_Sound", "WorldStateCoalesce", 0, iniPath.c_str()) != 0;
    }
}
