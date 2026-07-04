#pragma once

#ifndef SAVED_VARS_ASYNC_SERIALIZER_H
#define SAVED_VARS_ASYNC_SERIALIZER_H

namespace SavedVarsAsyncSerializer {

// Initialize the async SavedVariables serializer and hook the table serialization routines
bool Init();

// Shut down the hooks and worker thread
void Shutdown();

} // namespace SavedVarsAsyncSerializer

#endif // SAVED_VARS_ASYNC_SERIALIZER_H
