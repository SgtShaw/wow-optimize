#pragma once

#include <cstdint>

bool InstallStreamCache();
void UninstallStreamCache();
void GetStreamCacheStats(uint64_t* rHits, uint64_t* rTotal, uint64_t* wHits, uint64_t* wTotal);
