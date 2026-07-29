#pragma once

// ============================================================================
// Module: crt_free_hook.h
// Description: Removes the discarded _msize call from WoW's CRT free wrapper.
// Safety & Threading: Concurrent execution safe.
// ============================================================================

#include <cstdint>

bool InstallCrtFreeHook();
void UninstallCrtFreeHook();

// Prints how many deallocations went through the replacement, or says plainly
// that it was never installed - those are different facts and the log has to
// keep them apart.
void ReportCrtFreeStats();

void GetCrtFreeStats(uint64_t* hits, uint64_t* total);
