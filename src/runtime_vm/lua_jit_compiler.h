#pragma once

namespace LuaJitCompiler {
    bool Init();
    void Shutdown();
    bool ShouldCompile(void* proto);
}
