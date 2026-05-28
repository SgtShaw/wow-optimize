#pragma once
#include <cstdint>

bool InstallCrtFreeHook();
void UninstallCrtFreeHook();
void GetCrtFreeStats(uint64_t* hits, uint64_t* total);
