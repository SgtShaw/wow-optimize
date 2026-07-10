#pragma once
#include <windows.h>

namespace AuraUpdateDedup {
    bool Init();
    void Shutdown();
    bool ShouldSkipAuraUpdate(unsigned __int64 unitGuid, unsigned int spellId, bool isApplied);
}
