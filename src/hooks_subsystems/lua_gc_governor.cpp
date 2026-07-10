#include "lua_gc_governor.h"
#include "version.h"
#include <atomic>

extern "C" void Log(const char* fmt, ...);

namespace LuaGcGovernor {

typedef int (__cdecl *lua_gc_fn)(uintptr_t L, int what, int data);
static lua_gc_fn orig_lua_gc = nullptr;

static std::atomic<uint64_t> g_gcStepsRun{0};

bool Init() {
    // Resolve lua_gc dynamically
    HMODULE hWow = GetModuleHandleA(nullptr);
    orig_lua_gc = (lua_gc_fn)0x0084F140; // Address of lua_gc in WoW 3.3.5a
    Log("[LuaGcGovernor] Active - LUA Garbage Collector Budget Governor initialized");
    return true;
}

void Shutdown() {
    Log("[LuaGcGovernor] Stats: Performed %lld adaptive garbage collection steps", g_gcStepsRun.load());
}

void OnFrame(float elapsedMs) {
    if (elapsedMs <= 0.0f) return;

    // Only run extra GC steps if the current frame was processed very quickly (e.g. < 8.0ms, i.e. > 125 FPS)
    // This consumes idle CPU time for GC and prevents massive GC spikes during intense frames
    if (elapsedMs < 8.0f) {
        uintptr_t gL = *(uintptr_t*)0x00D3F78C; // Get active World VM state
        if (gL && orig_lua_gc) {
            __try {
                // Perform a tiny GC step (data = 100 corresponds to 100kb budget step)
                orig_lua_gc(gL, 5, 100); // LUA_GCSTEP is 5
                g_gcStepsRun.fetch_add(1, std::memory_order_relaxed);
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
}

} // namespace LuaGcGovernor
