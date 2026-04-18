#pragma once

#define WOW_OPTIMIZE_VERSION_MAJOR  3
#define WOW_OPTIMIZE_VERSION_MINOR  5
#define WOW_OPTIMIZE_VERSION_PATCH  8
#define WOW_OPTIMIZE_VERSION_BUILD  0

#define WOW_OPTIMIZE_VERSION_STR    "3.5.8"
#define WOW_OPTIMIZE_AUTHOR         "SUPREMATIST"

#ifndef CRASH_TEST_DISABLE_PHASE2
#define CRASH_TEST_DISABLE_PHASE2   0
#endif

// ================================================================
// PRODUCTION FLAGS — stable configuration
// ================================================================
#define TEST_DISABLE_ALL_APICACHE       1   // DISABLED: breaks Aux/WCollections/ElvUI
#define TEST_DISABLE_ALL_PHASE2         0
#define TEST_DISABLE_LUA_VM_OPT         0
#define TEST_DISABLE_PHASE2_WRITES      1
#define TEST_DISABLE_PHASE2_READS       1
#define TEST_DISABLE_PHASE2_NEW_DMA     0
#define TEST_DISABLE_GETSPELLINFO_CACHE 1

// ================================================================
// TEST FLAGS — individual new hooks (set 1 to DISABLE)
// ================================================================
#define TEST_DISABLE_HOOK_IPAIRS        1   // DISABLED — closure creation causes EXCEPTION crashes
#define TEST_DISABLE_HOOK_MATH_RANDOM   0   // ENABLED — tested stable
#define TEST_DISABLE_HOOK_MATH_SQRT     0   // ENABLED — tested stable
#define TEST_DISABLE_HOOK_STRING_REP    0   // ENABLED — tested stable
#define TEST_DISABLE_HOOK_STRING_FIND   0   // ENABLED — tested stable

// ================================================================
// NEW OPTIMIZATIONS — individual toggles (set 1 to DISABLE)
// ================================================================
#define TEST_DISABLE_LSTRLEN            0   // lstrlenA/W fast path
#define TEST_DISABLE_GETPROCADDRESS     1   // DISABLED: hash collision returns wrong FARPROC → login crash
#define TEST_DISABLE_MODULEFILENAME     1   // DISABLED: conflicts with OBS hook chain → crash + exit error
#define TEST_DISABLE_ENVVARIABLE        0   // GetEnvironmentVariable cache
#define TEST_DISABLE_MBWC               0   // MultiByteToWideChar/WideCharToMultiByte ASCII fast path
