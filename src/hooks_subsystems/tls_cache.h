#pragma once

// ============================================================================
// Module: tls_cache.h
// ============================================================================










#include <cstdint>

bool InstallTlsCache();
void UninstallTlsCache();
void GetTlsCacheStats(uint64_t* hits, uint64_t* total);

// TLS Cache Routines
void* OptimizeSub6238A0_TLSCache(unsigned int index);
void* OptimizeSub45E080_TLSCache();
void* OptimizeSub55BDC0_TLSCache(void* key);
void* OptimizeSub509DD0_TLSCache(const char* name);
