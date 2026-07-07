#pragma once
#include <string>

namespace CombatTextCoalescer {
    bool Init();
    void Shutdown();
    bool ProcessMessage(const std::string& text, std::string& outNewText);
}
