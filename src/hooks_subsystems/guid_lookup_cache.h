#pragma once

// ============================================================================
// Module: guid_lookup_cache.h
// Description: Lock-free GUID to Object pointer lookup cache.
// Safety & Threading: Thread-safe using atomic structures.
// ============================================================================

namespace GuidLookupCache {

bool Init();
void Shutdown();
void Invalidate(unsigned __int64 guid);

} // namespace GuidLookupCache
