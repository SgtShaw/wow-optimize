#pragma once

#ifndef SAVED_VARS_ASYNC_SERIALIZER_H
#define SAVED_VARS_ASYNC_SERIALIZER_H

#include <string>

namespace SavedVarsAsyncSerializer {

// Initialize the async SavedVariables serializer and hook the table serialization routines
bool Init();

// Shut down the hooks and worker thread
void Shutdown();

// Synchronously write pending variables or wait for active background writes to complete
void FlushFile(const std::string& filename);

} // namespace SavedVarsAsyncSerializer

#endif // SAVED_VARS_ASYNC_SERIALIZER_H
