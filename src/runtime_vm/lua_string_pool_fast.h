#pragma once
#include <string>

namespace LuaStringPoolFast {
    bool Init();
    void Shutdown();
    void* GetSymbol(const std::string& str);
    void InsertSymbol(const std::string& str, void* luaStringObj);

    // Feature 12 (0x0051D9B0): TLS-cached String Table Lookup
    void* OptimizeSub51D9B0_TLSString(const char* str, size_t len);
}
