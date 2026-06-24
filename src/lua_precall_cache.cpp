#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include "MinHook.h"
#include "version.h"
#include "crash_dumper.h"

extern "C" void Log(const char* fmt, ...);

#define CACHE_SIZE 1024
#define CACHE_MASK (CACHE_SIZE - 1)

struct Entry { uintptr_t closure; int nups; uint64_t hits; };
static Entry g_cache[CACHE_SIZE];
static volatile long g_hits = 0;
static volatile long g_misses = 0;

typedef int(__cdecl *precall_t)(uintptr_t,uintptr_t,int,uint64_t,uint64_t*);
static precall_t orig = nullptr;

void PrecallCache_Invalidate() { memset(g_cache, 0, sizeof(g_cache)); }

static int __cdecl hook(uintptr_t L, uintptr_t tv, int nres, uint64_t tin, uint64_t* tout)
{
    uintptr_t obj = *(uintptr_t*)(tv + 0);
    if (*(int*)(tv + 8) != 6 || obj < 0x10000) return orig(L,tv,nres,tin,tout);
    if (*(unsigned char*)(obj + 10) & 1) return orig(L,tv,nres,tin,tout);

    uint32_t s = ((uint32_t)(obj >> 4) * 2654435761u) & CACHE_MASK;
    if (g_cache[s].closure == obj) {
        g_hits++;
        g_cache[s].hits++;
    } else {
        g_misses++;
        g_cache[s].closure = obj;
        g_cache[s].nups = *(unsigned char*)(*(uintptr_t*)(obj + 24) + 79);
        g_cache[s].hits = 1;
    }
    return orig(L,tv,nres,tin,tout);
}

bool InstallLuaPrecallCache() {
    void* t = (void*)0x00856550;
    if (MH_CreateHook(t, hook, (void**)&orig) != MH_OK) return false;
    MH_EnableHook(t);
    Log("[PrecallCache] ACTIVE — 1024-entry closure metadata cache");
    CrashDumper::RegisterFeature("PrecallCache");
    CrashDumper::FeatureSetActive("PrecallCache", true);
    return true;
}
