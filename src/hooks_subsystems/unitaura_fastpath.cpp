// ============================================================================
// Module: unitaura_fastpath.cpp
// Description: Supporting utility functions for `unitaura_fastpath.cpp`.
// Safety & Threading: Verify pointer validation boundaries range up to 0xFFE00000.
// ============================================================================

#include "windows.h"
#include <stdint.h>

// AuraQuery structure from 3.3.5a
#pragma pack(push, 1)
struct AuraQuery {
    uint64_t guid;        // 0x00
    int32_t index;        // 0x08
    const char* name;     // 0x0C
    const char* rank;     // 0x10
    uint8_t flags;        // 0x14
};
#pragma pack(pop)

// Thread-local cache for O(1) lookups
static __declspec(thread) uint64_t t_lastGuid = 0;
static __declspec(thread) int32_t t_lastIndex = -1;
static __declspec(thread) uint8_t t_lastFlags = 0;
static __declspec(thread) int32_t t_lastPos = -1;

// Hook 1: Loop Entry (0x00614A76)
// Replaces:
// 614a76: 33 f6           xor esi, esi
// 614a78: 88 45 fb        mov [ebp-5], al
// 614a7b: 89 75 fc        mov [ebp-4], esi
extern "C" uint32_t __fastcall CheckAuraCache(AuraQuery* q, uint32_t* out_v42) {
    if (q->name == nullptr && q->rank == nullptr && q->index > 0) {
        if (q->guid == t_lastGuid && q->flags == t_lastFlags && q->index == t_lastIndex + 1) {
            // Cache hit! Resume scan from the NEXT aura after our last match
            *out_v42 = q->index;
            return t_lastPos + 1; // Return value goes into ESI
        }
    }
    
    // Cache miss or non-sequential lookup
    *out_v42 = 0;
    return 0; // Start ESI at 0
}

__declspec(naked) void Hook_UnitAura_Start() {
    __asm {
        // We are at 0x00614A76
        // edi = AuraQuery*
        // al = v41
        
        push eax
        push ecx
        push edx

        lea edx, [ebp-4]      // out_v42
        mov ecx, edi          // q
        call CheckAuraCache
        mov esi, eax          // Set starting loop index

        pop edx
        pop ecx
        pop eax

        // Original instructions we displaced
        mov [ebp-5], al       // [ebp-5] = al
        // mov [ebp-4], esi is omitted because CheckAuraCache sets it
        
        mov eax, 0x00614A7E
        jmp eax
    }
}

// Hook 2: Loop Return/Match (0x00614B4D)
// Replaces:
// 614b4d: 8b 45 f4        mov eax, [ebp-0Ch]
// 614b50: 8d 04 06        lea eax, [esi+eax]
extern "C" void __fastcall RecordAuraCache(AuraQuery* q, int32_t pos) {
    if (q->name == nullptr && q->rank == nullptr) {
        t_lastGuid = q->guid;
        t_lastIndex = q->index;
        t_lastFlags = q->flags;
        t_lastPos = pos;
    }
}

__declspec(naked) void Hook_UnitAura_Match() {
    __asm {
        // We are at 0x00614B4D
        // edi = AuraQuery*
        // esi = array index matched
        
        push eax
        push ecx
        push edx

        mov ecx, edi
        mov edx, esi
        call RecordAuraCache

        pop edx
        pop ecx
        pop eax

        // Original instructions we displaced
        mov eax, [ebp-0Ch]
        lea eax, [esi+eax]
        
        mov ecx, 0x00614B53
        jmp ecx
    }
}

#include "MinHook.h"
#include "version.h"

void InstallUnitAuraFastPath() {
    WineSafe_CreateHook((void*)0x00614A76, (void*)Hook_UnitAura_Start, nullptr);
    WineSafe_CreateHook((void*)0x00614B4D, (void*)Hook_UnitAura_Match, nullptr);
}
