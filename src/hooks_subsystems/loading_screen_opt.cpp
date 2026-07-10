#include "loading_screen_opt.h"
#include "../allocators/loading_defrag.h"

namespace LoadingScreenOpt {
    static bool g_enabled = true;

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    bool IsLoadingActive() {
        return g_enabled && LoadingDefrag::IsLoadingActive();
    }
}
