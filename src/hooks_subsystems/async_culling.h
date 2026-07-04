#pragma once

#ifndef ASYNC_CULLING_H
#define ASYNC_CULLING_H

#include <windows.h>

namespace AsyncCulling {

// Initialize the async culling module, hook culling APIs, and start task queues
bool Init();

// Shutdown the module and disable hooks
void Shutdown();

// Notify the module that a new frame has started (snapshots camera frustum planes)
void OnFrameStart();

} // namespace AsyncCulling

#endif // ASYNC_CULLING_H
