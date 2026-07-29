#pragma once
#include <string>

namespace LuaStringPoolFast {
    bool Init();
    void Shutdown();
void LogStats();
    void* GetSymbol(const std::string& str);
    void InsertSymbol(const std::string& str, void* luaStringObj);

}
