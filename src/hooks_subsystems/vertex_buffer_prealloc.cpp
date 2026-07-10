#include "vertex_buffer_prealloc.h"
#include "version.h"
#include <mutex>
#include <vector>

extern "C" void Log(const char* fmt, ...);

namespace VertexBufferPrealloc {

static constexpr size_t POOL_SIZE = 128;
static constexpr size_t CHUNK_SIZE = 65536; // 64KB chunks for vertex buffers

static uint8_t* g_poolBuffer = nullptr;
static void* g_freeChunks[POOL_SIZE];
static size_t g_freeCount = 0;
static std::mutex g_poolMutex;
static uint64_t g_allocations = 0;
static uint64_t g_poolHits = 0;

bool Init() {
    std::lock_guard<std::mutex> lock(g_poolMutex);
    g_poolBuffer = (uint8_t*)VirtualAlloc(nullptr, POOL_SIZE * CHUNK_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_poolBuffer) {
        g_freeCount = POOL_SIZE;
        for (size_t i = 0; i < POOL_SIZE; i++) {
            g_freeChunks[i] = g_poolBuffer + i * CHUNK_SIZE;
        }
        Log("[VertexBufferPrealloc] Pre-allocated %d chunks of %d bytes", POOL_SIZE, CHUNK_SIZE);
    }
    Log("[VertexBufferPrealloc] Active - Model Vertex Buffers Pre-Allocator initialized");
    return true;
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_poolMutex);
    if (g_poolBuffer) {
        VirtualFree(g_poolBuffer, 0, MEM_RELEASE);
        g_poolBuffer = nullptr;
    }
    Log("[VertexBufferPrealloc] Stats: Serviced %lld allocations, %lld pool hits", g_allocations, g_poolHits);
}

void* AllocateBuffer(size_t size) {
    std::lock_guard<std::mutex> lock(g_poolMutex);
    g_allocations++;
    if (size <= CHUNK_SIZE && g_freeCount > 0) {
        g_poolHits++;
        return g_freeChunks[--g_freeCount];
    }
    // Fallback to standard heap malloc
    return malloc(size);
}

void FreeBuffer(void* ptr) {
    if (!ptr) return;
    std::lock_guard<std::mutex> lock(g_poolMutex);
    if (g_poolBuffer && ptr >= g_poolBuffer && ptr < g_poolBuffer + POOL_SIZE * CHUNK_SIZE) {
        g_freeChunks[g_freeCount++] = ptr;
        return;
    }
    free(ptr);
}

} // namespace VertexBufferPrealloc
