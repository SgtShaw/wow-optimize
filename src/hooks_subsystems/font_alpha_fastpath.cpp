#include "font_alpha_fastpath.h"

namespace FontAlphaFastpath {
    static bool g_enabled = true;

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    bool ShouldApplyFastpath(unsigned int color) {
        if (!g_enabled) return false;

        // Extract alpha channel from ARGB/ABGR color representation
        // If alpha is fully opaque (0xFF) or fully transparent (0x00), 
        // we can skip alpha blending calculations and use a simple copy state or fast-path blend.
        unsigned int alpha = (color >> 24) & 0xFF;
        return (alpha == 0xFF || alpha == 0x00);
    }
}
