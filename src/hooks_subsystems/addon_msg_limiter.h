#pragma once
#include <windows.h>
#include <string>

namespace AddonMsgLimiter {
    bool Init();
    void Shutdown();
    bool ShouldSendAddonMessage(const std::string& prefix, const std::string& text);
}
