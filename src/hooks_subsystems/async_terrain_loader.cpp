#include "async_terrain_loader.h"
#include "../allocators/loading_defrag.h"
#include <windows.h>
#include <mutex>
#include <unordered_set>
#include <thread>
#include <atomic>
#include "../../build/_deps/minhook-src/include/MinHook.h"

extern "C" void Log(const char* fmt, ...);

namespace AsyncTerrainLoader {

typedef int (__cdecl *sub_7D9A20_fn)(void* grid);
static sub_7D9A20_fn orig_sub_7D9A20 = nullptr;

typedef int (__cdecl *sub_7C3700_fn)(void* grid);
static sub_7C3700_fn orig_sub_7C3700 = nullptr;

typedef int (__cdecl *sub_7C1660_fn)(float* pos, float* out_height, void** out_chunk);
static sub_7C1660_fn orig_sub_7C1660 = nullptr;

typedef void* (__thiscall *sub_7D6BF0_fn)(void* This, int a2, void* a3);
static sub_7D6BF0_fn orig_sub_7D6BF0 = nullptr;

static std::unordered_set<void*> g_loadingGrids;
static std::mutex g_loadingGridsMutex;
static std::atomic<int> g_asyncLoadCount{0};

bool IsGridLoading(void* grid) {
    if (!grid) return false;
    std::lock_guard<std::mutex> lock(g_loadingGridsMutex);
    return g_loadingGrids.count(grid) > 0;
}

// Separated helper function to comply with MSVC SEH constraints
static void SafeLoadAdt(void* grid, sub_7D9A20_fn orig_fn) {
    __try {
        orig_fn(grid);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("[AsyncTerrainLoader] Exception in background ADT load");
    }
}

int __cdecl Hooked_LoadAdt(void* grid) {
    if (!grid) return 0;

    // If loading screen is active, perform load synchronously
    if (LoadingDefrag::IsLoadingActive()) {
        return orig_sub_7D9A20(grid);
    }

    {
        std::lock_guard<std::mutex> lock(g_loadingGridsMutex);
        if (g_loadingGrids.count(grid)) {
            return 0; // Already loading
        }
        g_loadingGrids.insert(grid);
    }

    g_asyncLoadCount.fetch_add(1, std::memory_order_relaxed);

    std::thread([grid]() {
        SafeLoadAdt(grid, orig_sub_7D9A20);

        {
            std::lock_guard<std::mutex> lock(g_loadingGridsMutex);
            g_loadingGrids.erase(grid);
        }
        g_asyncLoadCount.fetch_sub(1, std::memory_order_relaxed);
    }).detach();

    return 1;
}

int __cdecl Hooked_UnloadGrid(void* grid) {
    if (grid) {
        // Wait if the grid is currently loading in the background
        while (true) {
            {
                std::lock_guard<std::mutex> lock(g_loadingGridsMutex);
                if (g_loadingGrids.count(grid) == 0) {
                    break;
                }
            }
            Sleep(1);
        }
    }
    return orig_sub_7C3700(grid);
}

int __cdecl Hooked_GetGroundElevation(float* pos, float* out_height, void** out_chunk) {
    int result = orig_sub_7C1660(pos, out_height, out_chunk);
    if (!result) {
        // If query failed but an async load is active, supply current Z position as height fallback
        if (g_asyncLoadCount.load(std::memory_order_relaxed) > 0) {
            if (out_height && pos) {
                *out_height = pos[2];
                if (out_chunk) *out_chunk = nullptr;
                return 1;
            }
        }
    }
    return result;
}

void* __fastcall Hooked_CMapGrid_Update(void* This, void* unused, int a2, void* a3) {
    if (This && IsGridLoading(This)) {
        return a3; // Skip updating sub-chunks while ADT is still loading in background
    }
    return orig_sub_7D6BF0(This, a2, a3);
}

bool Init() {
    Log("--- Asynchronous Terrain Mesh Loader & Collision Decoupler ---");

    if (MH_CreateHook((LPVOID)0x007D9A20, (LPVOID)Hooked_LoadAdt, (LPVOID*)&orig_sub_7D9A20) != MH_OK) {
        Log("[AsyncTerrainLoader] Failed to hook LoadAdt (0x7D9A20)");
        return false;
    }
    if (MH_CreateHook((LPVOID)0x007C3700, (LPVOID)Hooked_UnloadGrid, (LPVOID*)&orig_sub_7C3700) != MH_OK) {
        Log("[AsyncTerrainLoader] Failed to hook UnloadGrid (0x7C3700)");
        return false;
    }
    if (MH_CreateHook((LPVOID)0x007C1660, (LPVOID)Hooked_GetGroundElevation, (LPVOID*)&orig_sub_7C1660) != MH_OK) {
        Log("[AsyncTerrainLoader] Failed to hook GetGroundElevation (0x7C1660)");
        return false;
    }
    if (MH_CreateHook((LPVOID)0x007D6BF0, (LPVOID)Hooked_CMapGrid_Update, (LPVOID*)&orig_sub_7D6BF0) != MH_OK) {
        Log("[AsyncTerrainLoader] Failed to hook CMapGrid::Update (0x7D6BF0)");
        return false;
    }

    if (MH_EnableHook((LPVOID)0x007D9A20) != MH_OK ||
        MH_EnableHook((LPVOID)0x007C3700) != MH_OK ||
        MH_EnableHook((LPVOID)0x007C1660) != MH_OK ||
        MH_EnableHook((LPVOID)0x007D6BF0) != MH_OK) {
        Log("[AsyncTerrainLoader] Failed to enable hooks");
        return false;
    }

    Log("[AsyncTerrainLoader] Active - Asynchronous ADT loading & collision decoupling active");
    return true;
}

void Shutdown() {
    MH_DisableHook((LPVOID)0x007D9A20);
    MH_DisableHook((LPVOID)0x007C3700);
    MH_DisableHook((LPVOID)0x007C1660);
    MH_DisableHook((LPVOID)0x007D6BF0);

    // Wait for all background loads to finish
    while (g_asyncLoadCount.load(std::memory_order_relaxed) > 0) {
        Sleep(1);
    }

    std::lock_guard<std::mutex> lock(g_loadingGridsMutex);
    g_loadingGrids.clear();
}

} // namespace AsyncTerrainLoader
