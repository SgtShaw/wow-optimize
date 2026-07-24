#pragma once
#ifndef ASYNC_TERRAIN_LOADER_H
#define ASYNC_TERRAIN_LOADER_H

namespace AsyncTerrainLoader {
    bool Init();
    void Shutdown();
    bool IsGridLoading(void* grid);

    // Feature 29 (0x0094BD50): Asynchronous Terrain Chunk Loader
    void OptimizeSub94BD50_AsyncTerrain(float* chunkPos, int mapId);
}

#endif // ASYNC_TERRAIN_LOADER_H
