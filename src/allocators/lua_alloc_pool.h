#pragma once

// ============================================================================
// Module: lua_alloc_pool.h
// Description: Thread-local allocation cache for Lua VM small temporary objects.
// Safety & Threading: Thread-safe, implements local free lists per thread.
// ============================================================================

namespace LuaAllocPool {

bool Init();
void Shutdown();

} // namespace LuaAllocPool
