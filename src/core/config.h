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
        bool OptVulkanDXVK = false;
        bool OptTimingFix = false;
        bool OptCvarNullGuard = true; // Safe default: enabled
        bool OptFrameLimiter = false;
        bool OptMpqMmapVfs = false;
        bool OptMpqPrefetch = false;
        bool OptObjVisCache = false;
        bool OptDbcPreload = false;
        bool OptOomGovernor = false;

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
        bool OptAsyncTexLoader = false;
        bool OptAsyncTerrainLoader = false;
        bool OptRcuObjMgr = false;
        bool OptM2LodBias = false;
        bool OptMipBiasGovernor = false;
        bool OptSpatialCulling = false;
        
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
        bool OptD3d9RenderThread = false;

        // 10 new features
        bool OptLoadingScreenOpt = true;
        bool OptCombatLogFilter = true;
        bool OptSoundVolumeLimit = true;
        bool OptUILayoutThrottle = true;
        bool OptTerrainHeightCache = true;
        bool OptAnimBlendCache = true;
        bool OptSavedVarsOpt = true;
        bool OptItemDataPrefetch = true;
        bool OptMovementSmoothing = true;
        bool OptFontAlphaFastpath = true;

        // 20 new colossal features (Features 31-50)
        bool OptPacketProcessingThrottle = true;
        bool OptNameplateCulling = true;
        bool OptTextureUnloadDelay = false;
        bool OptM2MatrixSimd = true;
        bool OptMinimapRefreshGovernor = true;
        bool OptSpellEffectCulling = true;
        bool OptLuaStringCompareFast = true;
        bool OptDbcRowCaching = true;
        bool OptNetworkStringDedup = true;
        bool OptCameraCollisionThrottle = true;
        bool OptSoundFreqCoalesce = true;
        bool OptAuraUpdateDedup = true;
        bool OptUiTextureCaching = true;
        bool OptWmoCullingOpt = true;
        bool OptFastFloatParse = true;
        bool OptHeapAllocationTracker = true;
        bool OptSpellCooldownCache = true;
        bool OptGuidStringCache = true;
        bool OptFrameScriptMemOpt = true;
        bool OptCombatEventLimit = true;
    };

    extern Settings g_settings;

    // Load settings from wow_opt.ini
    void Load();
}

#endif // WOW_OPT_CONFIG_H
