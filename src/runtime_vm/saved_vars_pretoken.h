#pragma once
#include <windows.h>

namespace SavedVarsPretoken {
    bool Init();
    void Shutdown();
    void OnCreateFile(HANDLE hFile, const char* filename, DWORD dwAccess);
    void OnCloseHandle(HANDLE hFile);
    bool TryServe(HANDLE hFile, LPVOID lpBuffer, DWORD nBytes, LPDWORD lpBytesRead);
    bool GetMinifiedFileSize(HANDLE hFile, PLARGE_INTEGER lpFileSize);
}
