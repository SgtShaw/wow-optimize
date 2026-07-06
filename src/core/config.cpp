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
            // Write default ini template
            WritePrivateProfileStringA("Optimizations", "SavedVarsSerializer", "0", iniPath.c_str());
            WritePrivateProfileStringA("Optimizations", "PacketOffload", "0", iniPath.c_str());
            WritePrivateProfileStringA("Optimizations", "FrameLimiter", "0", iniPath.c_str());
            WritePrivateProfileStringA("Optimizations", "SleepPrecision", "8", iniPath.c_str());
            WritePrivateProfileStringA("Optimizations", "MemoryPressure", "1", iniPath.c_str());
            WritePrivateProfileStringA("Optimizations", "HeapCompactor", "1", iniPath.c_str());
            WritePrivateProfileStringA("Optimizations", "UIFrameBatch", "1", iniPath.c_str());
        }

        g_settings.SavedVarsSerializer = GetPrivateProfileIntA("Optimizations", "SavedVarsSerializer", 0, iniPath.c_str()) != 0;
        g_settings.PacketOffload       = GetPrivateProfileIntA("Optimizations", "PacketOffload", 0, iniPath.c_str()) != 0;
        g_settings.FrameLimiter        = GetPrivateProfileIntA("Optimizations", "FrameLimiter", 0, iniPath.c_str()) != 0;
        g_settings.SleepPrecision      = GetPrivateProfileIntA("Optimizations", "SleepPrecision", 8, iniPath.c_str());
        g_settings.MemoryPressure      = GetPrivateProfileIntA("Optimizations", "MemoryPressure", 1, iniPath.c_str()) != 0;
        g_settings.HeapCompactor       = GetPrivateProfileIntA("Optimizations", "HeapCompactor", 1, iniPath.c_str()) != 0;
        g_settings.UIFrameBatch        = GetPrivateProfileIntA("Optimizations", "UIFrameBatch", 1, iniPath.c_str()) != 0;
    }
}
