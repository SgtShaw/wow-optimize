#pragma once

// ============================================================================
// Module: ui_accessor_fast.h
// ============================================================================









#ifndef UI_ACCESSOR_FAST_H
#define UI_ACCESSOR_FAST_H

bool InstallUIAccessorFast();
void ShutdownUIAccessorFast();

// Feature 37 (0x00906270): SSE2 Vectorized UI Vertex Transform
void OptimizeSub906270_UIVertexTransform(float* inVertices, float* outVertices, const float* matrix4x4, int count);

#endif // UI_ACCESSOR_FAST_H
