// ================================================================
// Object Visibility Cache
// Caches GUID -> visibility results from WoW's object filter function
// (sub_4D4DB0) to eliminate redundant TLS + hash table lookups during
// per-frame world update passes.
// ================================================================
#pragma once
#include <cstdint>
#include <windows.h>

namespace ObjVisCache {

bool Init();
void Shutdown();
void OnFrame();  // Call once per frame on main thread to invalidate stale entries

} // namespace ObjVisCache
