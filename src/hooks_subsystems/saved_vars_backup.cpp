#include "saved_vars_backup.h"

namespace SavedVarsBackup {

bool Init() {
    return true;
}

void Shutdown() {
    // No-op
}

void CreateBackup(const std::string& filePath) {
    if (filePath.empty()) return;
    
    // Only backup WoW Lua WTF configurations (SavedVariables)
    if (filePath.find("WTF") != std::string::npos && filePath.find(".lua") != std::string::npos) {
        std::string backupPath = filePath + ".bak";
        // Simple file copy via Windows API
        CopyFileA(filePath.c_str(), backupPath.c_str(), FALSE);
    }
}

} // namespace SavedVarsBackup
