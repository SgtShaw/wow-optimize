#pragma once

// Frame Script Throttling
bool InstallFrameThrottling();
void GetFrameThrottleStats(long* skipped, long* executed, long* bypassed);
void ShutdownFrameThrottling();
