#pragma once

// ============================================================================
// Module: regex_cache.h
// Description: Supporting utility functions for `regex_cache.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================

#include <cstdint>

bool InstallRegexCache();
void ShutdownRegexCache();
void RegexCache_Clear();

const uint8_t* RegexCache_Get(const char* pattern, int patternLen, unsigned int options, const unsigned char* tableptr, int* outCompiledLen);
void RegexCache_Put(const char* pattern, int patternLen, unsigned int options, const unsigned char* tableptr, const uint8_t* compiled, int compiledLen);