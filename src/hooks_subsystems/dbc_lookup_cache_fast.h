#pragma once
#include <string>

namespace DbcLookupCacheFast {
    bool Init();
    void Shutdown();
    void LogStats();
    bool LookupName(uint32_t id, std::string& outName);
    void InsertName(uint32_t id, const std::string& name);
}
