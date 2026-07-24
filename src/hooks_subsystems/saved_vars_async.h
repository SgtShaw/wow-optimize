#pragma once

// ============================================================================
// Module: saved_vars_async.h
// ============================================================================









// SavedVariables Async Writer
// Offloads SV serialization to background thread so logout/reload
// doesn't block the main thread on large addon data writes.
bool InstallSavedVarsAsync();
void ShutdownSavedVarsAsync();
void FlushSavedVarsAsyncSynchronously();

// Feature 48 (0x00739650): Asynchronous SavedVariables Serializer
void OptimizeSub739650_AsyncSavedVars(const char* luaFilePath, const char* dataBuffer);
