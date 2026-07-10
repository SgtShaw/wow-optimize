#include "lua_string_compare_fast.h"
#include <cstring>

namespace LuaStringCompareFast {
    static bool g_enabled = true;

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    int CompareStringsSse(const char* s1, const char* s2, size_t len) {
        if (!g_enabled || !s1 || !s2) return 0;
        // Optimized comparison via compiler-inlined SIMD memcmp
        return std::memcmp(s1, s2, len);
    }
}
