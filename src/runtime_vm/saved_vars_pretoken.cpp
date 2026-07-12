#include "saved_vars_pretoken.h"
#include "version.h"
#include <string>
#include <vector>

extern "C" void Log(const char* fmt, ...);

namespace SavedVarsPretoken {

void OnCreateFile(HANDLE hFile, const char* filename, DWORD dwAccess) {
    // Disabled for stability
}

void OnCloseHandle(HANDLE hFile) {
    // Disabled for stability
}

bool TryServe(HANDLE hFile, LPVOID lpBuffer, DWORD nBytes, LPDWORD lpBytesRead) {
    return false; // Disabled for stability
}

bool GetMinifiedFileSize(HANDLE hFile, PLARGE_INTEGER lpFileSize) {
    return false; // Disabled for stability
}

bool Init() {
    Log("[SavedVarsPretoken] Preloader disabled for stability.");
    return false;
}

void Shutdown() {
    // Disabled for stability
}

} // namespace SavedVarsPretoken
