#pragma once

#define WOW_OPTIMIZE_VERSION_MAJOR  3
#define WOW_OPTIMIZE_VERSION_MINOR  5
#define WOW_OPTIMIZE_VERSION_PATCH  0
#define WOW_OPTIMIZE_VERSION_BUILD  0

#define WOW_OPTIMIZE_VERSION_STR    "3.5.0"
#define WOW_OPTIMIZE_AUTHOR         "SUPREMATIST"

// Crash isolation toggle for Phase 2 Lua fast paths
// Set to 1 to disable Phase 2 hooks for debugging
#ifndef CRASH_TEST_DISABLE_PHASE2
#define CRASH_TEST_DISABLE_PHASE2   0
#endif

// ================================================================
// PRODUCTION FLAGS — v3.5.0 stable configuration
// ================================================================
#define TEST_DISABLE_ALL_APICACHE   0
#define TEST_DISABLE_ALL_PHASE2     0
#define TEST_DISABLE_LUA_VM_OPT     0
#define TEST_DISABLE_PHASE2_WRITES  1   // PERMANENT: rawset, insert, remove, next
#define TEST_DISABLE_PHASE2_READS   1   // PERMANENT: rawget, concat, unpack
#define TEST_DISABLE_PHASE2_NEW_DMA 0   // Reverted to safe Lua API calls