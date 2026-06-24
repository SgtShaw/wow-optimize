// SEH guard for per-frame sound update (sub_508320, covers 0x508950)
// Shares the underlying hook with sound_buffer_guard (same function)
#pragma once

bool InstallSoundUpdateGuard();
void UninstallSoundUpdateGuard();
