#pragma once
#include <string>

namespace SavedVarsPreloadAsync {
    bool Init();
    void Shutdown();
    void QueuePreload(const std::string& filePath);
    bool GetPreloadedContent(const std::string& filePath, std::string* outContent);
}
