// ================================================================
// Frame-Scoped Event Coalescer (Synchronous Deduplication)
// ================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"

extern "C" void Log(const char* fmt, ...);

// Address of FrameScript_SignalEvent in 3.3.5a 12340
static constexpr uintptr_t ADDR_FrameScript_SignalEvent = 0x0081AC90;

typedef void(__cdecl *FrameScript_SignalEvent_t)(int eventId, const char* format, ...);
static void* orig_FrameScript_SignalEvent = nullptr;

// We use a small direct-mapped hash table to track seen events this frame
static constexpr int HASH_BITS = 10;
static constexpr int HASH_SIZE = 1 << HASH_BITS;
static constexpr int HASH_MASK = HASH_SIZE - 1;

struct EventEntry {
    int eventId;
    uintptr_t arg1;
    uint32_t generation;
};

static EventEntry g_eventCache[HASH_SIZE];
static uint32_t g_currentGeneration = 1;
static uint32_t g_eventsDropped = 0;
static uint32_t g_eventsTotal = 0;

// Call this at the end of every frame (e.g., in Sleep hook or OnFrame)
extern "C" void EventCoalescer_NextFrame() {
    g_currentGeneration++;
    if (g_currentGeneration == 0) {
        g_currentGeneration = 1; // avoid 0
    }
}

// Returns true if the event is a duplicate THIS FRAME
bool __fastcall CheckEventDuplicate(int eventId, const char* format, uintptr_t arg1) {
    g_eventsTotal++;
    
    // We only care about high-spam events (e.g. UNIT_HEALTH, UNIT_POWER, UNIT_AURA)
    // and we hash them by (eventId ^ arg1). 
    // arg1 is usually a pointer to a string ("player", "target") or a GUID part.
    uint32_t hash = ((uint32_t)eventId ^ (uint32_t)arg1) & HASH_MASK;
    
    EventEntry& entry = g_eventCache[hash];
    if (entry.generation == g_currentGeneration && 
        entry.eventId == eventId && 
        entry.arg1 == arg1) {
        // It's a duplicate!
        g_eventsDropped++;
        return true; 
    }
    
    // Record it
    entry.generation = g_currentGeneration;
    entry.eventId = eventId;
    entry.arg1 = arg1;
    
    return false;
}

// Naked hook to intercept FrameScript_SignalEvent without disturbing the va_list
__declspec(naked) void Hooked_FrameScript_SignalEvent() {
    __asm {
        // __cdecl arguments:
        // [esp]   = return address
        // [esp+4] = eventId
        // [esp+8] = format
        // [esp+12] = arg1 (first vararg)
        
        mov ecx, dword ptr [esp+4]  // eventId (fastcall arg 1)
        mov edx, dword ptr [esp+8]  // format (fastcall arg 2)
        mov eax, dword ptr [esp+12] // arg1
        
        push eax // fastcall stack arg
        call CheckEventDuplicate
        test al, al
        jnz drop_event
        
        // Not a duplicate, jump to original
        jmp [orig_FrameScript_SignalEvent]
        
    drop_event:
        // Duplicate: just return. 
        // The caller is __cdecl, so it will clean up the stack.
        ret
    }
}

namespace EventCoalescer {
    bool Init() {
        Log("[EventCoalescer] Initializing Frame-Scoped Event Deduplication");
        
        // Validate prologue (push ebp; mov ebp, esp)
        unsigned char* p = (unsigned char*)ADDR_FrameScript_SignalEvent;
        if (p[0] != 0x55 || p[1] != 0x8B) {
            Log("[EventCoalescer] BAD PROLOGUE at 0x%08X", ADDR_FrameScript_SignalEvent);
            return false;
        }

        if (WineSafe_CreateHook((void*)ADDR_FrameScript_SignalEvent, (void*)Hooked_FrameScript_SignalEvent, &orig_FrameScript_SignalEvent) != MH_OK) {
            Log("[EventCoalescer] Failed to hook FrameScript_SignalEvent");
            return false;
        }
        
        if (MH_EnableHook((void*)ADDR_FrameScript_SignalEvent) != MH_OK) {
            Log("[EventCoalescer] Failed to enable hook");
            return false;
        }
        
        for (int i = 0; i < HASH_SIZE; i++) {
            g_eventCache[i].generation = 0;
        }
        
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
