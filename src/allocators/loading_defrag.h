#pragma once

#ifndef LOADING_DEFRAG_H
#define LOADING_DEFRAG_H

#include <windows.h>

namespace LoadingDefrag {

// Initialize the loading defragmenter background thread and data structures
bool Init();

// Shutdown the background thread and clean up resources
void Shutdown();

// Notify the module about changes in the game's loading screen state
void NotifyLoadingState(bool isLoading);

// Call every frame from the main thread (e.g. to update zone text)
void OnFrame();

} // namespace LoadingDefrag

#endif // LOADING_DEFRAG_H
