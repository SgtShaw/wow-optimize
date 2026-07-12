// ============================================================================
// Module: event_coalescer.cpp
// Description: Supporting utility functions for `event_coalescer.cpp`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <cstring>
#include "MinHook.h"
#include "version.h"

#include "runtime_vm/lua_gc_governor.h"

extern "C" void Log(const char* fmt, ...);

static constexpr uintptr_t ADDR_FrameScript_SignalEvent = 0x0081AC90;

typedef void(__cdecl *FrameScript_SignalEvent_t)(int eventId, const char* format, ...);
static void* orig_FrameScript_SignalEvent = nullptr;

// Fixed-size event entry — no C++ objects, safe for SEH
struct QueuedEvent {
    int eventId;
    char format[8];        // longest observed format is "%s%s" (5 chars)
    char strArg1[32];      // unitId strings like "player", "party1target" etc
    char strArg2[32];
    int intArg1;
    bool hasStr1;
    bool hasStr2;
    bool hasInt1;
    bool used;
};

static constexpr int MAX_QUEUED = 256;
static QueuedEvent g_queue[MAX_QUEUED];
static int g_queueCount = 0;
static thread_local bool g_isReplaying = false;
static SRWLOCK g_eventLock = SRWLOCK_INIT;

static uint32_t g_eventsTotal = 0;
static uint32_t g_eventsDropped = 0;

static const char* GetEventName(int eventId) {
    __try {
        if (eventId < 0 || eventId > 2000) return nullptr;
        uintptr_t* eventTable = *(uintptr_t**)0x00D3F7D8;
        if (!eventTable) return nullptr;

        uintptr_t eventPtr = eventTable[eventId];
        if (eventPtr < 0x10000 || eventPtr > 0xFFE00000) return nullptr;

        const char* name = *(const char**)(eventPtr + 20);
        if (name < (const char*)0x10000 || name > (const char*)0xFFE00000) return nullptr;

        return name;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static bool ShouldCoalesce(const char* name) {
    if (!name) return false;
    if (strcmp(name, "BAG_UPDATE") == 0 ||
        strcmp(name, "SPELL_UPDATE_COOLDOWN") == 0 ||
        strcmp(name, "UNIT_POWER") == 0) {
        return true;
    }
    return false;
}

static bool IsDuplicate(const QueuedEvent* ev) {
    for (int i = 0; i < g_queueCount; ++i) {
        const QueuedEvent* e = &g_queue[i];
        if (!e->used) continue;
        if (e->eventId != ev->eventId) continue;
        if (e->hasStr1 != ev->hasStr1) continue;
        if (e->hasStr1 && strcmp(e->strArg1, ev->strArg1) != 0) continue;
        if (e->hasStr2 != ev->hasStr2) continue;
        if (e->hasStr2 && strcmp(e->strArg2, ev->strArg2) != 0) continue;
        if (e->hasInt1 != ev->hasInt1) continue;
        if (e->hasInt1 && e->intArg1 != ev->intArg1) continue;
        return true;
    }
    return false;
}

static void DispatchSingle(const QueuedEvent* ev) {
    __try {
        auto fn = (FrameScript_SignalEvent_t)orig_FrameScript_SignalEvent;
        if (ev->hasStr1 && ev->hasStr2) {
            fn(ev->eventId, ev->format, ev->strArg1, ev->strArg2);
        } else if (ev->hasStr1) {
            fn(ev->eventId, ev->format, ev->strArg1);
        } else if (ev->hasInt1) {
            fn(ev->eventId, ev->format, ev->intArg1);
        } else {
            fn(ev->eventId, ev->format);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // guard replay
    }
}

// Called from the naked hook — parse format string and queue if coalescable
#include "combat_log_filter.h"

extern "C" bool __fastcall TryQueueEvent(int eventId, const char* format, void* vaStart) {
    if (g_isReplaying) return false;

    const char* eventName = GetEventName(eventId);
    if (eventName && strcmp(eventName, "COMBAT_LOG_EVENT_UNFILTERED") == 0) {
        if (CombatLogFilter::ShouldFilterEvent(eventId, format, (va_list)vaStart)) {
            return true; // Drop (filter) the event
        }
    }

    static bool s_coalesceCache[4096] = {};
    static bool s_coalesceChecked[4096] = {};
    static bool s_combatStateEvents[4096] = {}; // to set combat/loading flags

    // Fall back to slow path for out-of-bounds event IDs (extremely rare)
    if (eventId < 0 || eventId >= 4096) {
        const char* eventName = GetEventName(eventId);
        if (!eventName) return false;
        if (strcmp(eventName, "PLAYER_REGEN_DISABLED") == 0) {
            LuaGCGovernor::g_inCombat = true;
        } else if (strcmp(eventName, "PLAYER_REGEN_ENABLED") == 0) {
            LuaGCGovernor::g_inCombat = false;
        } else if (strcmp(eventName, "PLAYER_LEAVING_WORLD") == 0) {
            LuaGCGovernor::g_isLoading = true;
        } else if (strcmp(eventName, "PLAYER_ENTERING_WORLD") == 0) {
            LuaGCGovernor::g_isLoading = false;
        }
        return ShouldCoalesce(eventName);
    }

    if (!s_coalesceChecked[eventId]) {
        const char* eventName = GetEventName(eventId);
        if (eventName) {
            if (strcmp(eventName, "PLAYER_REGEN_DISABLED") == 0) {
                LuaGCGovernor::g_inCombat = true;
                s_combatStateEvents[eventId] = true;
            } else if (strcmp(eventName, "PLAYER_REGEN_ENABLED") == 0) {
                LuaGCGovernor::g_inCombat = false;
                s_combatStateEvents[eventId] = true;
            } else if (strcmp(eventName, "PLAYER_LEAVING_WORLD") == 0) {
                LuaGCGovernor::g_isLoading = true;
                s_combatStateEvents[eventId] = true;
            } else if (strcmp(eventName, "PLAYER_ENTERING_WORLD") == 0) {
                LuaGCGovernor::g_isLoading = false;
                s_combatStateEvents[eventId] = true;
            } else if (ShouldCoalesce(eventName)) {
                s_coalesceCache[eventId] = true;
            }
        }
        s_coalesceChecked[eventId] = true;
    } else {
        // Fast path for combat/loading state updates
        if (s_combatStateEvents[eventId]) {
            const char* eventName = GetEventName(eventId);
            if (eventName) {
                if (strcmp(eventName, "PLAYER_REGEN_DISABLED") == 0) {
                    LuaGCGovernor::g_inCombat = true;
                } else if (strcmp(eventName, "PLAYER_REGEN_ENABLED") == 0) {
                    LuaGCGovernor::g_inCombat = false;
                } else if (strcmp(eventName, "PLAYER_LEAVING_WORLD") == 0) {
                    LuaGCGovernor::g_isLoading = true;
                } else if (strcmp(eventName, "PLAYER_ENTERING_WORLD") == 0) {
                    LuaGCGovernor::g_isLoading = false;
                }
            }
        }
    }

    if (!s_coalesceCache[eventId]) {
        return false;
    }

    QueuedEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.eventId = eventId;
    ev.used = true;

    if (format) {
        size_t fmtLen = strlen(format);
        if (fmtLen >= sizeof(ev.format)) fmtLen = sizeof(ev.format) - 1;
        memcpy(ev.format, format, fmtLen);
        ev.format[fmtLen] = '\0';
    }

    // Parse varargs from the stack based on the format string
    __try {
        uintptr_t* pArgs = (uintptr_t*)vaStart;
        if (format) {
            const char* p = format;
            while (*p) {
                if (*p == '%') {
                    p++;
                    if (*p == '\0') break;
                    char type = *p;
                    if (type == 's') {
                        const char* s = (const char*)*pArgs;
                        pArgs++;
                        if (s >= (const char*)0x10000 && s < (const char*)0xFFE00000) {
                            if (!ev.hasStr1) {
                                strncpy(ev.strArg1, s, sizeof(ev.strArg1) - 1);
                                ev.strArg1[sizeof(ev.strArg1) - 1] = '\0';
                                ev.hasStr1 = true;
                            } else if (!ev.hasStr2) {
                                strncpy(ev.strArg2, s, sizeof(ev.strArg2) - 1);
                                ev.strArg2[sizeof(ev.strArg2) - 1] = '\0';
                                ev.hasStr2 = true;
                            }
                        }
                    } else if (type == 'd' || type == 'u' || type == 'b') {
                        int val = (int)*pArgs;
                        pArgs++;
                        ev.intArg1 = val;
                        ev.hasInt1 = true;
                    } else if (type == 'f') {
                        // skip double (8 bytes on stack)
                        pArgs += 2;
                    }
                }
                p++;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false; // failed to parse, let original handle it
    }

    AcquireSRWLockExclusive(&g_eventLock);
    g_eventsTotal++;

    if (g_queueCount >= MAX_QUEUED) {
        ReleaseSRWLockExclusive(&g_eventLock);
        return false; // queue full, let it through
    }

    if (IsDuplicate(&ev)) {
        g_eventsDropped++;
        ReleaseSRWLockExclusive(&g_eventLock);
        return true; // Drop duplicate (it's already queued to be dispatched at the end of the frame)
    }

    g_queue[g_queueCount++] = ev;
    ReleaseSRWLockExclusive(&g_eventLock);
    return true; // Defer: return true to skip immediate execution!
}

__declspec(naked) void Hooked_FrameScript_SignalEvent() {
    __asm {
        // [esp+0]  = return address
        // [esp+4]  = eventId
        // [esp+8]  = format
        // [esp+12] = first vararg

        mov ecx, dword ptr [esp+4]   // eventId -> fastcall arg1
        mov edx, dword ptr [esp+8]   // format  -> fastcall arg2
        lea eax, [esp+12]            // &first vararg
        push eax                     // stack arg for __fastcall
        call TryQueueEvent

        test al, al
        jnz drop_event

        jmp [orig_FrameScript_SignalEvent]

    drop_event:
        ret
    }
}

extern "C" void EventCoalescer_Flush() {
    AcquireSRWLockExclusive(&g_eventLock);
    g_isReplaying = true;
    int count = g_queueCount;
    QueuedEvent localQueue[MAX_QUEUED];
    memcpy(localQueue, g_queue, count * sizeof(QueuedEvent));
    g_queueCount = 0;
    ReleaseSRWLockExclusive(&g_eventLock);

    for (int i = 0; i < count; ++i) {
        if (localQueue[i].used) {
            DispatchSingle(&localQueue[i]);
        }
    }

    AcquireSRWLockExclusive(&g_eventLock);
    g_isReplaying = false;
    ReleaseSRWLockExclusive(&g_eventLock);
}

namespace EventCoalescer {
    bool Init() {
        Log("[EventCoalescer] Initializing Frame-Scoped Event Deduplication");

        unsigned char* p = (unsigned char*)ADDR_FrameScript_SignalEvent;
        if (p[0] != 0x55 || p[1] != 0x8B) {
            Log("[EventCoalescer] BAD PROLOGUE at 0x%08X", ADDR_FrameScript_SignalEvent);
            return false;
        }

        if (WineSafe_CreateHook((void*)ADDR_FrameScript_SignalEvent, (void*)Hooked_FrameScript_SignalEvent, &orig_FrameScript_SignalEvent) != MH_OK) {
            Log("[EventCoalescer] Failed to hook FrameScript_SignalEvent");
            return false;
        }

        if (WO_EnableHook((void*)ADDR_FrameScript_SignalEvent) != MH_OK) {
            Log("[EventCoalescer] Failed to enable hook");
            return false;
        }

        memset(g_queue, 0, sizeof(g_queue));
        Log("[EventCoalescer] ACTIVE (Hooked at 0x%08X)", ADDR_FrameScript_SignalEvent);
        return true;
    }

    void Shutdown() {
        if (orig_FrameScript_SignalEvent) {
            MH_DisableHook((void*)ADDR_FrameScript_SignalEvent);
        }
        if (g_eventsTotal > 0) {
            Log("[EventCoalescer] Stats: Total %u, Dropped %u (%.1f%% reduction)",
                g_eventsTotal, g_eventsDropped,
                100.0 * g_eventsDropped / g_eventsTotal);
        }
    }
}
