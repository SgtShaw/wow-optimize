#include "vertex_buffer_prealloc.h"
#include "version.h"
#include <vector>

extern "C" void Log(const char* fmt, ...);

namespace VertexBufferPrealloc {

static constexpr size_t POOL_SIZE = 128;
static constexpr size_t CHUNK_SIZE = 65536; // 64KB chunks for vertex buffers

static uint8_t* g_poolBuffer = nullptr;
static void* g_freeChunks[POOL_SIZE];
static size_t g_freeCount = 0;
static SRWLOCK g_poolLock = SRWLOCK_INIT;
static uint64_t g_allocations = 0;
static uint64_t g_poolHits = 0;

struct SRWLockGuard {
    SRWLOCK* lock;
    SRWLockGuard(SRWLOCK* l) : lock(l) { AcquireSRWLockExclusive(lock); }
    ~SRWLockGuard() { ReleaseSRWLockExclusive(lock); }
};

bool Init() {
    SRWLockGuard lock(&g_poolLock);
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
    SRWLockGuard lock(&g_poolLock);
    if (g_poolBuffer) {
        VirtualFree(g_poolBuffer, 0, MEM_RELEASE);
        g_poolBuffer = nullptr;
    }
    Log("[VertexBufferPrealloc] Stats: Serviced %lld allocations, %lld pool hits", g_allocations, g_poolHits);
}

void* AllocateBuffer(size_t size) {
    SRWLockGuard lock(&g_poolLock);
    g_allocations++;
    if (size <= CHUNK_SIZE && g_freeCount > 0 && g_freeCount <= POOL_SIZE) {
        g_poolHits++;
        return g_freeChunks[--g_freeCount];
    }
    // Fallback to 16-byte aligned malloc
    return _aligned_malloc(size, 16);
}

void FreeBuffer(void* ptr) {
    if (!ptr) return;
    SRWLockGuard lock(&g_poolLock);
    if (g_poolBuffer && ptr >= g_poolBuffer && ptr < g_poolBuffer + POOL_SIZE * CHUNK_SIZE) {
        // Double-free and bounds-overflow guard
        if (g_freeCount >= POOL_SIZE) {
            Log("[VertexBufferPrealloc] WARNING: Pool overflow detected in FreeBuffer (g_freeCount=%d). Bypassing pool push.", g_freeCount);
            return;
        }
        for (size_t i = 0; i < g_freeCount; i++) {
            if (g_freeChunks[i] == ptr) {
                Log("[VertexBufferPrealloc] WARNING: Double free detected for pointer %p", ptr);
                return;
            }
        }
        g_freeChunks[g_freeCount++] = ptr;
        return;
    }
    _aligned_free(ptr);
}

// Feature 52 (0x004DAB40): Memory Pool Slot Allocator
static __declspec(align(16)) uint8_t s_poolSlotStorage[32768];
static volatile LONG s_poolSlotOffset = 0;

void* OptimizeSub4DAB40_PoolSlotAlloc(size_t size) {
    if (size == 0 || size > 2048) return nullptr;
    size = (size + 15) & ~15;
    LONG off = InterlockedExchangeAdd(&s_poolSlotOffset, (LONG)size);
    if ((size_t)off + size > sizeof(s_poolSlotStorage)) {
        InterlockedExchangeAdd(&s_poolSlotOffset, -(LONG)size);
        return nullptr;
    }
    return s_poolSlotStorage + off;
}

// Feature 59 (0x004C74F0): Object Memory Pool Slot
static __declspec(align(16)) uint8_t s_objPoolStorage[32768];
static volatile LONG s_objPoolOffset = 0;

void* OptimizeSub4C74F0_ObjPoolAlloc(size_t size) {
    if (size == 0 || size > 2048) return nullptr;
    size = (size + 15) & ~15;
    LONG off = InterlockedExchangeAdd(&s_objPoolOffset, (LONG)size);
    if ((size_t)off + size > sizeof(s_objPoolStorage)) {
        InterlockedExchangeAdd(&s_objPoolOffset, -(LONG)size);
        return nullptr;
    }
    return s_objPoolStorage + off;
}

// Feature 60 (0x00927A40): Lock-Free Atomic Slot Memory Update
bool OptimizeSub927A40_LockFreeUpdate(volatile LONG* slot, const void* src, void* dst, size_t size) {
    if (!slot || !src || !dst || size == 0) return false;
    if (InterlockedCompareExchange(slot, 1, 0) == 0) {
        memcpy(dst, src, size);
        InterlockedExchange(slot, 0);
        return true;
    }
    return false;
}

} // namespace VertexBufferPrealloc
