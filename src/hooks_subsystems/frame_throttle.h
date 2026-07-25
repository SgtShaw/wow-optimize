#pragma once

// ============================================================================
// Module: frame_throttle.h
// ============================================================================










// Frame Script Throttling
bool InstallFrameThrottling();
void GetFrameThrottleStats(long* skipped, long* executed, long* bypassed);
void ShutdownFrameThrottling();

// Execution Throttling Governors
bool OptimizeSub5C29C0_ThrottleUpdate(DWORD currentTick);
bool OptimizeSub542030_ThrottleUIRefresh(DWORD currentTick);
