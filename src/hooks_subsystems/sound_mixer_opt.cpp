#include "sound_mixer_opt.h"
#include "MinHook.h"
#include "version.h"
#include <windows.h>
#include <psapi.h>
#include <cstdint>

extern "C" void Log(const char* fmt, ...);

namespace SoundMixerOpt {

typedef HANDLE (WINAPI *CreateThread_fn)(
    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    SIZE_T dwStackSize,
    LPTHREAD_START_ROUTINE lpStartAddress,
    LPVOID lpParameter,
    DWORD dwCreationFlags,
    LPDWORD lpThreadId
);
static CreateThread_fn orig_CreateThread = nullptr;

static HANDLE WINAPI Hooked_CreateThread(
    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    SIZE_T dwStackSize,
    LPTHREAD_START_ROUTINE lpStartAddress,
    LPVOID lpParameter,
    DWORD dwCreationFlags,
    LPDWORD lpThreadId
) {
    HANDLE hThread = orig_CreateThread(lpThreadAttributes, dwStackSize, lpStartAddress, lpParameter, dwCreationFlags, lpThreadId);
    if (hThread) {
        HMODULE hFmod = GetModuleHandleA("fmod32.dll");
        if (hFmod) {
            MODULEINFO mi;
            if (GetModuleInformation(GetCurrentProcess(), hFmod, &mi, sizeof(mi))) {
                uintptr_t addr = (uintptr_t)lpStartAddress;
                uintptr_t base = (uintptr_t)mi.lpBaseOfDll;
                if (addr >= base && addr < base + mi.SizeOfImage) {
                    SetThreadPriority(hThread, THREAD_PRIORITY_ABOVE_NORMAL);
                    Log("[SoundMixerOpt] Elevated priority for FMOD thread: ID=%d", lpThreadId ? *lpThreadId : 0);
                }
            }
        }
    }
    return hThread;
}

bool Init() {
    #if !TEST_DISABLE_SOUND_MIXER_OPT
    void* target = (void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateThread");
    if (target) {
        if (MH_CreateHook(target, (void*)Hooked_CreateThread, (void**)&orig_CreateThread) == MH_OK) {
            MH_EnableHook(target);
            Log("[SoundMixerOpt] Hooked CreateThread to optimize FMOD thread priority");
            return true;
        }
    }
    #endif
    return true;
}

void Shutdown() {
    #if !TEST_DISABLE_SOUND_MIXER_OPT
    void* target = (void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateThread");
    if (target) {
        MH_DisableHook(target);
    }
    #endif
}

} // namespace SoundMixerOpt
