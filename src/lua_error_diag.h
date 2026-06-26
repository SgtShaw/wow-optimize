#pragma once

// Installs a Lua error diagnostic hook.
// NOTE: 0x84F610 is IDA-verified as sub_84F610(size_t Size) = luaL_addvalue,
// NOT lua_error. The correct lua_error address must be found via IDA before
// re-enabling this hook. Gated by TEST_DISABLE_LUA_ERROR_DIAG in version.h.
bool InstallLuaErrorDiag();
