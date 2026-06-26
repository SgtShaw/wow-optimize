#pragma once

// Installs the Lua error diagnostic hook at 0x84F610.
// Logs every Lua error message + hook trace to the log file.
// This is always-on diagnostics; never crash-disabled.
bool InstallLuaErrorDiag();
