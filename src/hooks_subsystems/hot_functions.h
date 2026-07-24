#pragma once

// ============================================================================
// Module: hot_functions.h
// ============================================================================









#include <cstdint>

bool InstallHotFunctionOptimizations();
void UninstallHotFunctionOptimizations();

// Feature 5 (0x00476DB0): Inlined Accessor Function
int OptimizeSub476DB0_InlineAccessor(int objectPtr);

// Feature 38 (0x006CA330): Inlined Field Copy
void OptimizeSub6CA330_InlineCopy(void* dest, const void* src);

// Feature 39 (0x00909330): Inlined Short-Circuit Evaluator
int OptimizeSub909330_InlineEval(int thisPtr, short a2, int a3);

// Feature 41 (0x00508320): Inlined Pair Comparison
int OptimizeSub508320_InlinePairCmp(int a1, int a2);

// Feature 44 (0x006EF860): Inlined String Copy with Length
int OptimizeSub6EF860_InlineStrCopy(int thisPtr, int srcPtr);
