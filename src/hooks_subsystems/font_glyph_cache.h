#pragma once
#include <windows.h>

namespace FontGlyphCache {
    struct GlyphData {
        int width;
        int height;
        int advance;
        void* textureData;
    };

    bool Init();
    void Shutdown();
    bool GetGlyph(uint32_t charCode, GlyphData* outData);
    void InsertGlyph(uint32_t charCode, const GlyphData& data);
}
