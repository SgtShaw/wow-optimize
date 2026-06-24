// SEH guard for sound driver init / mode toggle (sub_508260)
#pragma once

bool InstallSoundDriverGuard();
void UninstallSoundDriverGuard();
