// SEH guard for sound buffer / chat-channel config (sub_508320, covers 0x508740)
#pragma once

bool InstallSoundBufferGuard();
void UninstallSoundBufferGuard();
