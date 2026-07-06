#pragma once
#ifndef WOW_OPT_CONFIG_H
#define WOW_OPT_CONFIG_H

namespace Config {
    struct Settings {
        bool SavedVarsSerializer = false; // default false
        bool PacketOffload = false;       // default false
        bool FrameLimiter = false;        // default false
        int SleepPrecision = 8;           // default 8ms
        bool MemoryPressure = true;       // default true
        bool HeapCompactor = true;        // default true
        bool UIFrameBatch = true;         // default true
    };

    extern Settings g_settings;

    // Load settings from wow_opt.ini
    void Load();
}

#endif // WOW_OPT_CONFIG_H
