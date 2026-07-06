// ============================================================================
// Module: ui_frame_batch.cpp
// Description: Supporting utility functions for `ui_frame_batch.cpp`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================

#include <windows.h>
#include "MinHook.h"
#include "version.h"
#include "config.h"

extern "C" void Log(const char* fmt, ...);

// ================================================================
// UI Frame Update Batching
// ================================================================

// Stats
static volatile long g_batchedFrames = 0;      // Number of frame update batches processed
static volatile long g_totalIterations = 0;    // Total frame update iterations
static volatile long g_peakIterations = 0;     // Peak iterations in a single batch

// Global flag that the original function checks (dword_B41834)
static DWORD* g_frameUpdatePending = (DWORD*)0x00B41834;

// Original frame update loop at 0x004800F0
// Signature: int __usercall@<eax>(int@<ebx>, int@<edi>, int)
// This uses a custom calling convention with ebx, edi registers + stack parameter
// We need to use a naked function to preserve the exact calling convention
typedef int (__cdecl* FrameUpdateLoop_fn)();  // Will be called via inline asm
static FrameUpdateLoop_fn orig_FrameUpdateLoop = nullptr;

// ================================================================
// Hooked Frame Update Loop - Naked function to preserve registers
// ================================================================
__declspec(naked) static int Hooked_FrameUpdateLoop() {
#if TEST_DISABLE_UI_FRAME_BATCH
    __asm {
        jmp dword ptr [orig_FrameUpdateLoop]
    }
#else
    __asm {
        // Preserve all registers
        push ebp
        mov ebp, esp
        pushad
        
        // Read dword_B41834 to count pending updates
        mov eax, dword ptr [0x00B41834]
        mov ecx, eax
        xor edx, edx
        
        // Count bits in ecx
    count_loop:
        test ecx, ecx
        jz count_done
        mov eax, ecx
        and eax, 1
        add edx, eax
        shr ecx, 1
        jmp count_loop
        
    count_done:
        // edx now contains iteration count
        // Update g_batchedFrames
        lock inc dword ptr [g_batchedFrames]
        
        // Update g_totalIterations
        lock add dword ptr [g_totalIterations], edx
        
        // Update g_peakIterations if needed
        mov eax, dword ptr [g_peakIterations]
    peak_loop:
        cmp edx, eax
        jle peak_done
        lock cmpxchg dword ptr [g_peakIterations], edx
        jne peak_loop
        
    peak_done:
        // Restore registers
        popad
        pop ebp
        
        // Jump to original function
        jmp dword ptr [orig_FrameUpdateLoop]
    }
#endif
}

// ================================================================
// Installation
// ================================================================
bool InstallUIFrameBatching() {
    if (!Config::g_settings.UIFrameBatch) {
        Log("[UIFrameBatch] DISABLED via configuration (wow_opt.ini)");
        return false;
    }
#if TEST_DISABLE_UI_FRAME_BATCH
    Log("[UIFrameBatch] DISABLED (test toggle)");
    return false;
#else
    // Hook the frame update loop at 0x004800F0
    // This function loops while dword_B41834 is set and processes frame updates
    void* targetAddr = (void*)0x004800F0;
    
    if (MH_CreateHook(targetAddr, (void*)Hooked_FrameUpdateLoop, (void**)&orig_FrameUpdateLoop) != MH_OK) {
        Log("[UIFrameBatch] Failed to hook frame update loop");
        return false;
    }
    if (MH_EnableHook(targetAddr) != MH_OK) {
        Log("[UIFrameBatch] Failed to enable frame update loop hook");
        return false;
    }

    Log("[UIFrameBatch] ACTIVE (hooked frame update loop at 0x004800F0)");
    return true;
#endif
}

// ================================================================
// Stats
// ================================================================
void GetUIFrameBatchStats(long* batched, long* individual, long* frames) {
    if (batched) *batched = g_batchedFrames;
    if (individual) *individual = g_totalIterations;
    if (frames) *frames = g_peakIterations;
}

// ================================================================
// Cleanup
// ================================================================
void ShutdownUIFrameBatching() {
    // Nothing to clean up - no dynamic allocations
}
