#pragma once
#include <windows.h>

namespace SpellEffectCulling {
    bool Init();
    void Shutdown();
    bool ShouldCullEffect(unsigned int effectId);
}
