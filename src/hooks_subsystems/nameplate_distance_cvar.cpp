#include "nameplate_distance_cvar.h"
#include <cstdio>
#include <cstring>

namespace NameplateDistanceCvar {

typedef char (__thiscall *CVar_Set_fn)(void* This, const char* value, char a3, char a4, char a5, char a6);
static CVar_Set_fn orig_CVar_Set = (CVar_Set_fn)0x007668C0;

static void* g_nameplateDistCVar = nullptr;
static float g_maxDistance = 40.0f; // Default WoW 3.3.5a distance is 41 yards

void RegisterCVar(const char* name, void* cvar) {
    if (name && strcmp(name, "nameplateMaxDistance") == 0) {
        g_nameplateDistCVar = cvar;
    }
}

} // namespace NameplateDistanceCvar

extern "C" void RegisterNameplateCVar(const char* name, void* cvar) {
    NameplateDistanceCvar::RegisterCVar(name, cvar);
}

namespace NameplateDistanceCvar {

bool Init() {
    return true;
}

void Shutdown() {
    // No-op
}

float GetMaxDistance() {
    if (g_nameplateDistCVar) {
        // Retrieve distance value from CVar
        float* valPtr = (float*)((char*)g_nameplateDistCVar + 0x1C); // Value offset in CVar
        if (valPtr) {
            g_maxDistance = *valPtr;
        }
    }
    return g_maxDistance;
}

} // namespace NameplateDistanceCvar
