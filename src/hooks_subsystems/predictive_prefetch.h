#pragma once

#ifndef PREDICTIVE_PREFETCH_H
#define PREDICTIVE_PREFETCH_H

namespace PredictivePrefetch {

// Initialize the predictive prefetcher
bool Init();

// Tick called per frame to track velocity and prefetch files
void OnFrame();

// Shut down resources
void Shutdown();

} // namespace PredictivePrefetch

#endif // PREDICTIVE_PREFETCH_H
