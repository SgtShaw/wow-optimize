#pragma once

#define WOW_OPTIMIZE_VERSION_MAJOR  3
#define WOW_OPTIMIZE_VERSION_MINOR  5
#define WOW_OPTIMIZE_VERSION_PATCH  12
#define WOW_OPTIMIZE_VERSION_BUILD  0

#define WOW_OPTIMIZE_VERSION_STR    "3.5.12"
#define WOW_OPTIMIZE_AUTHOR         "SUPREMATIST"

#ifndef CRASH_TEST_DISABLE_PHASE2
#define CRASH_TEST_DISABLE_PHASE2   0
#endif

// ================================================================
// FLAG CONVENTION:
//   0 = ENABLED  (feature is active)
//   1 = DISABLED (feature is skipped at runtime)
//
// Every flag is a TEST_DISABLE_* name.
// Setting a flag to 1 surgically removes one feature for
// bisection builds — see build_test_variants.sh.
// ================================================================

// ================================================================
// PRODUCTION FLAGS — stable v3.5.11 configuration
// ================================================================

// GetItemInfo cache — disabled: breaks Aux / WCollections / ElvUI
// (Morbent, 9 test builds). GetSpellInfo hook also disabled below.
#define TEST_DISABLE_ALL_APICACHE       1

// Phase 2 Lua fast paths — enabled: stable since v3.5.5
#define TEST_DISABLE_ALL_PHASE2         0

// Lua VM GC optimizer + mimalloc allocator replacement — enabled
#define TEST_DISABLE_LUA_VM_OPT         0

// Phase 2 write hooks (rawset, insert, remove, next) — disabled:
// direct RawTValue* table writes caused hangs in real gameplay
#define TEST_DISABLE_PHASE2_WRITES      1

// Phase 2 read hooks (rawget, concat, unpack) — disabled:
// direct RawTValue* stack writes caused hangs in real gameplay
#define TEST_DISABLE_PHASE2_READS       1

// Phase 2 DMA hooks (type, floor, ceil, abs, max, min, len, byte,
// tostring, tonumber, select, rawequal) — enabled: stable
#define TEST_DISABLE_PHASE2_NEW_DMA     0

// GetSpellInfo cache — disabled: icon corruption + relog crash
#define TEST_DISABLE_GETSPELLINFO_CACHE 1

// ================================================================
// INDIVIDUAL PHASE 2 HOOK TOGGLES
// ================================================================

// ipairs factory hook — disabled: closure creation causes
// EXCEPTION crashes (architectural mismatch: factory vs. iterator)
#define TEST_DISABLE_HOOK_IPAIRS        1

// math.random — enabled: stable, tested
#define TEST_DISABLE_HOOK_MATH_RANDOM   0

// math.sqrt — enabled: stable, tested
#define TEST_DISABLE_HOOK_MATH_SQRT     0

// string.rep — enabled: stable, tested
#define TEST_DISABLE_HOOK_STRING_REP    0

// string.find (plain mode) — enabled: stable, tested
#define TEST_DISABLE_HOOK_STRING_FIND   0

// ================================================================
// WIN32 HOOK TOGGLES
// ================================================================

// lstrlenA/W inline fast path — enabled: cleared of alt-tab crash
// via 3-way bisection in v3.5.10 (MBWC/LSTRLEN/ENV isolation)
#define TEST_DISABLE_LSTRLEN            0

// GetProcAddress cache — disabled: hash collision returns wrong
// FARPROC on 512-slot direct-mapped table → login crash
#define TEST_DISABLE_GETPROCADDRESS     1

// GetModuleFileNameA/W cache — disabled: conflicts with OBS hook
// chain → crash + exit error reported in production
#define TEST_DISABLE_MODULEFILENAME     1

// GetEnvironmentVariableA cache — disabled: confirmed alt-tab
// NULL-deref crash, isolated via 3-way bisection in v3.5.10
#define TEST_DISABLE_ENVVARIABLE        1

// MultiByteToWideChar / WideCharToMultiByte SSE2 ASCII fast path —
// enabled: cleared of alt-tab crash via 3-way bisection in v3.5.10
#define TEST_DISABLE_MBWC               0

// CRT strlen/strcmp/memcmp/memcpy/memset SSE2 fast paths —
// disabled: entering-world crash, not yet bisected to root cause
#define TEST_DISABLE_CRT_MEM_FASTPATHS  1

// Deferred unit field update queue — disabled: UI/texture
// flickering due to immediate-mode rendering mismatch (v3.5.x)
#define TEST_DISABLE_DEFERRED_FIELD_UPDATES 1

// Hardware cursor fix (ShowCursor + ClipCursor, no hooks) —
// enabled: stable, shipped in v3.5.11
#define TEST_DISABLE_HARDWARE_CURSOR    0

// Async MPQ I/O predictive read-ahead queue —
// enabled: stable, shipped in v3.5.11
#define TEST_DISABLE_ASYNC_MPQ_IO       0

// table.sort fast path — disabled: persistent 0x00000004 AV on
// HD clients due to corrupted table pointers
#define TEST_DISABLE_TABLE_SORT_FASTPATH    1

// string.gsub fast path — disabled: luaS_newlstr crashes on HD
// clients due to % replacement semantics and buffer edge cases
#define TEST_DISABLE_STRING_GSUB_FASTPATH   1

// GetSystemMetrics cache — disabled: 0% real-session hit rate,
// removed for cleanup
#define TEST_DISABLE_SYSTEM_METRICS_CACHE   1

// Unit API fast paths — disabled: causes ElvUI breakage and character select AV
// Requires full taint propagation handling and UI-state guards before re-enabling.
#define TEST_DISABLE_UNIT_API_FASTPATH 1