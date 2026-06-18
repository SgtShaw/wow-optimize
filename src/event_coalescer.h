#pragma once

extern "C" void EventCoalescer_NextFrame();

namespace EventCoalescer {
    bool Init();
    void Shutdown();
}
