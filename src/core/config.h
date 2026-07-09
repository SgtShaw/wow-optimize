#pragma once
#ifndef WOW_OPT_CONFIG_H
#define WOW_OPT_CONFIG_H

namespace Config {
    struct Settings {
        // General & Memory
        bool OptSleepPrecision = true;
        int SleepPrecisionValue = 8;
        bool OptMemoryPressure = true;
        bool OptHeapCompactor = true;
        bool OptDefragLf = false;
        bool OptAllocators = false;
        bool OptVulkanDXVK = false;
        bool OptTimingFix = false;
        bool OptCvarNullGuard = true; // Safe default: enabled
        bool OptFrameLimiter = false;

        // UI & Lua
        bool OptUIFrameBatch = false;
        bool OptAddonDispatcher = false;
        bool OptUIFrameAccessorFast = false;
        bool OptFontMetricsFast = false;
        bool OptFontMetricsLockFree = false;
        bool OptFrameXmlCoalesce = false;
        bool OptAddonTickGovernor = false;
        bool OptTooltipCache = false;
        bool OptLuaFileCache = false;
        bool OptFrameScriptDispatch = false;
        bool OptLuaNumConvFast = false;
        bool OptLuaOpcache = false;
        bool OptLuaGcCoalesce = false;
        bool OptLuaJIT = false;
        
        // Combat & Network
        bool OptCombatLogParser = false;
        bool OptCombatLogIncremental = false;
        bool OptEventCoalescer = false;
        bool OptNetPacketCoalesce = false;
        bool OptUnitAuraCoalesce = false;
        bool OptNetAddonCoalescer = false;
        bool OptSavedVarsSerializer = false;
        bool OptSavedVarsAsync = false;
        bool OptSavedVarsPretoken = false;
        bool OptUnitAuraFast = false;
        bool OptNetworkGuidSse2 = false;
        bool OptGetSpellInfoCache = false;
        bool OptPacketOffload = false;
        bool OptNameplateMT = false;

        // Graphics & Sound
        bool OptStrStrSse2 = false;
        bool OptStrCatFast = false;
        bool OptSoundMixerOpt = false;
        bool OptAudioDecodeMt = false;
        bool OptDbcLookupCache = false;
        bool OptWorldStateCoalesce = false;
    };

    extern Settings g_settings;

    // Load settings from wow_opt.ini
    void Load();
}

#endif // WOW_OPT_CONFIG_H
