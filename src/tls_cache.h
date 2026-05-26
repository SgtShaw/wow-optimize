#pragma once

#include <cstdint>

bool InstallTlsCache();
void UninstallTlsCache();
void GetTlsCacheStats(uint64_t* hits, uint64_t* total);
