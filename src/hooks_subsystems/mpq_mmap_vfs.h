#pragma once
#ifndef WOW_OPT_MPQ_MMAP_VFS_H
#define WOW_OPT_MPQ_MMAP_VFS_H

#include <windows.h>
#include <string>
#include <vector>

namespace MpqMmapVfs {
    struct Stats {
        long filesCached;
        long cacheHits;
        long cacheMisses;
        long queueDepth;
        double totalLoadTimeMs;
    };

    bool Init();
    void Shutdown();
    void OnFrame();
    Stats GetStats();

    // Trigger pre-load and parallel decompression of a file
    void QueueFilePreload(const std::string& filename);

    // Add raw decompressed file bytes directly to the cache (e.g. for textures)
    void AddCachedFile(const std::string& path, std::vector<uint8_t>&& data);

    // VFS direct hook access for unified hook routing
    bool GetCachedFileData(const std::string& path, std::vector<uint8_t>& outData);
}

#endif // WOW_OPT_MPQ_MMAP_VFS_H
