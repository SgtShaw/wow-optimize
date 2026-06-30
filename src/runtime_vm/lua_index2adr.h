#pragma once

// ============================================================================
// Module: lua_index2adr.h
// Description: Accelerates Lua runtime calls in `lua_index2adr.h`.
// Safety & Threading: Thread-safe under Lua VM execution constraints.
// ============================================================================


/**
 * @domain: Lua VM C-API Fast Path
 * @architecture: Optimizes C-API transitions for Lua state queries in `lua_index2adr.h`.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Always maintain the Lua stack index alignment to prevent top index desynchronization.
 */



/**
 * @domain: Lua Virtual Machine Engine
 * @architecture: Fastpath detour hooks mapping hottest Lua VM interpreter instructions directly to C-level structures.
 * @thread_affinity: Main Loop / Thread-Safe worker constraints
 * @regression_hazard: Incorrect Lua stack balance adjustments or thread-local storage collisions will result in UI freeze and transition crashes.
 */


#include <cstdint>

// index2adr (sub_84D9C0) resolves a Lua stack/pseudo index to its TValue*.
// It is a __usercall leaf: the index arrives in EAX, the lua_State* in ECX, and
// the result is returned in EAX. MSVC cannot express __usercall, so this naked
// thunk marshals an ordinary __cdecl(idx, L) call into the EAX/ECX the engine
// reads. Invoking 0x84D9C0 through a plain __cdecl function pointer (which passes
// both args on the stack) leaves EAX/ECX holding unrelated garbage, so the engine
// dereferences a bogus pointer and faults at index2adr+4 (mov edx,[ecx+0x10]).
extern "C" uintptr_t __cdecl WowIndex2Adr(int idx, uintptr_t L);
