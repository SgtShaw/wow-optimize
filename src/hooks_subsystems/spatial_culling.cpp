#include "spatial_culling.h"
#include "version.h"
#include <atomic>
#include <cstring>

namespace SpatialCulling {

struct GridCell {
    std::atomic<int> count;
};

static constexpr int GRID_SIZE = 128;
static constexpr float CELL_SIZE = 10.0f;
static GridCell g_grid[GRID_SIZE][GRID_SIZE] = {};

static float* const g_playerX = (float*)0x00BE1F30;
static float* const g_playerY = (float*)0x00BE1F34;

void OnFrame() {
    #if !TEST_DISABLE_SPATIAL_CULLING
    for (int x = 0; x < GRID_SIZE; x++) {
        for (int y = 0; y < GRID_SIZE; y++) {
            g_grid[x][y].count.store(0, std::memory_order_relaxed);
        }
    }
    #endif
}

float GetSpatialCullBias(void* model, float distance) {
    #if !TEST_DISABLE_SPATIAL_CULLING
    if (!g_playerX || !g_playerY) return 1.0f;
    
    float px = *g_playerX;
    float py = *g_playerY;
    
    int cellX = (int)(px / CELL_SIZE) % GRID_SIZE;
    int cellY = (int)(py / CELL_SIZE) % GRID_SIZE;
    if (cellX < 0) cellX += GRID_SIZE;
    if (cellY < 0) cellY += GRID_SIZE;
    
    int cellDist = (int)(distance / CELL_SIZE);
    int targetX = (cellX + cellDist) % GRID_SIZE;
    int targetY = (cellY + cellDist) % GRID_SIZE;
    
    g_grid[targetX][targetY].count.fetch_add(1, std::memory_order_relaxed);
    
    int localCount = 0;
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            int cx = (targetX + dx + GRID_SIZE) % GRID_SIZE;
            int cy = (targetY + dy + GRID_SIZE) % GRID_SIZE;
            localCount += g_grid[cx][cy].count.load(std::memory_order_relaxed);
        }
    }
    
    if (localCount > 30) {
        if (distance > 40.0f) return 2.5f;
        if (distance > 20.0f) return 1.6f;
    }
    #endif
    return 1.0f;
}

bool Init() {
    return true;
}

void Shutdown() {
}

} // namespace SpatialCulling
