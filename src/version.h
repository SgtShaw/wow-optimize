#pragma once

#define WOW_OPTIMIZE_VERSION_MAJOR  3
#define WOW_OPTIMIZE_VERSION_MINOR  5
#define WOW_OPTIMIZE_VERSION_PATCH  5
#define WOW_OPTIMIZE_VERSION_BUILD  0

#define WOW_OPTIMIZE_VERSION_STR    "3.5.5"
#define WOW_OPTIMIZE_AUTHOR         "SUPREMATIST"

#ifndef CRASH_TEST_DISABLE_PHASE2
#define CRASH_TEST_DISABLE_PHASE2   0
#endif

// ================================================================
// PRODUCTION FLAGS — stable configuration
// ================================================================
#define TEST_DISABLE_ALL_APICACHE       1   // DISABLED: breaks Aux/WCollections/ElvUI
#define TEST_DISABLE_ALL_PHASE2         0   // Phase 2 Lua fast paths — ENABLED (tested stable by Morbent + Billy Hoyle, FPS 97-158)
#define TEST_DISABLE_LUA_VM_OPT         0
#define TEST_DISABLE_PHASE2_WRITES      1
#define TEST_DISABLE_PHASE2_READS       1
#define TEST_DISABLE_PHASE2_NEW_DMA     0
#define TEST_DISABLE_GETSPELLINFO_CACHE 1
