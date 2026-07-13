#include "cdatastore_buffering.h"
#include <unordered_map>
#include "win_mutex.h"

namespace CDataStoreBuffering {

struct BuffState {
    void* buffer;
    uint32_t size;
};

static std::unordered_map<void*, BuffState> g_datastoreCache;
static WinMutex g_dsMutex;
static uint64_t g_hits = 0;

bool Init() {
    return true;
}

void Shutdown() {
    WinLockGuard lock(g_dsMutex);
    g_datastoreCache.clear();
}

void* GetBufferedData(void* dataStore, uint32_t offset) {
    if (!dataStore) return nullptr;
    
    WinLockGuard lock(g_dsMutex);
    auto it = g_datastoreCache.find(dataStore);
    if (it != g_datastoreCache.end()) {
        if (offset < it->second.size) {
            g_hits++;
            return (char*)it->second.buffer + offset;
        }
    } else {
        // Read structure layout of CDataStore
        // CDataStore structure usually has buffer pointer at offset 0x10 and size at 0x14
        void** bufPtr = (void**)((char*)dataStore + 0x10);
        uint32_t* szPtr = (uint32_t*)((char*)dataStore + 0x14);
        if (bufPtr && szPtr && *bufPtr) {
            g_datastoreCache[dataStore] = { *bufPtr, *szPtr };
            if (offset < *szPtr) {
                return (char*)*bufPtr + offset;
            }
        }
    }
    
    return nullptr;
}

} // namespace CDataStoreBuffering
