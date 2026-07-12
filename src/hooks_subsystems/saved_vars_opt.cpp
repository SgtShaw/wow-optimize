#include "saved_vars_opt.h"
#include <stdio.h>
#include <string.h>

namespace SavedVarsOpt {
    static bool g_enabled = true;

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    bool OptimizeSerialization(const char* filepath) {
        return false;
    }
}
