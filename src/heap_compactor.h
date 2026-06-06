#pragma once

#include "version.h"

#if TEST_DISABLE_HEAP_COMPACTOR == 0

bool HeapCompactor_Init();
void HeapCompactor_Shutdown();

// Diagnostic queries
extern "C" SIZE_T HeapCompactor_GetLargestFreeBlock();
extern "C" void HeapCompactor_GetStats(uint64_t* checks, uint64_t* compactions,
                                        SIZE_T* lastBlock, SIZE_T* minBlock, SIZE_T* maxBlock);

#else

inline bool HeapCompactor_Init() { return true; }
inline void HeapCompactor_Shutdown() {}

#endif
