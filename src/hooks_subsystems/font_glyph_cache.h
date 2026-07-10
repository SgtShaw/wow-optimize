#pragma once

// ============================================================================
// Module: font_glyph_cache.h
// Description: Pre-caching atlas allocator for font glyph textures.
// Safety & Threading: Thread-safe, lock-guarded find/insert operations.
// ============================================================================

namespace FontGlyphCache {
    bool Init();
    void Shutdown();
}
