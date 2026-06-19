#pragma once

extern "C" void EventCoalescer_Flush();

namespace EventCoalescer {
    bool Init();
    void Shutdown();
}
