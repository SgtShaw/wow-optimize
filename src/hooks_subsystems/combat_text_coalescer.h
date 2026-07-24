#pragma once
#include <string>

namespace CombatTextCoalescer {
    bool Init();
    void Shutdown();
    bool ProcessMessage(const std::string& text, std::string& outNewText);

    // Feature 45 (0x007385C0): Throttled Floating Combat Text Routine
    bool OptimizeSub7385C0_CombatTextThrottle(int amount, char isCrit);
}
