#pragma once

// ============================================================================
// Module: regex_cache.h
// Description: Supporting utility functions for `regex_cache.h`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================


/**
 * @domain: Client Optimizer Support Subsystem
 * @architecture: Implements helper methods and utility wrappers for `regex_cache.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Address validation checks must range up to 0xFFE00000 to support high-address LAA allocations.
 */



/**
 * @domain: Lua Virtual Machine Engine
 * @architecture: Fastpath detour hooks mapping hottest Lua VM interpreter instructions directly to C-level structures.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Incorrect Lua stack balance adjustments or thread-local storage collisions will result in UI freeze and transition crashes.
 */



#include <cstdint>

bool InstallRegexCache();
void ShutdownRegexCache();
void RegexCache_Clear();

const uint8_t* RegexCache_Get(const char* pattern, int patternLen, int* outCompiledLen);
void RegexCache_Put(const char* pattern, int patternLen, const uint8_t* compiled, int compiledLen);