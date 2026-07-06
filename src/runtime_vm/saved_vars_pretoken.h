#pragma once
#include <windows.h>

namespace SavedVarsPretoken {
    bool Init();
    void Shutdown();
    void OnCreateFile(HANDLE hFile, const char* filename);
    void OnCloseHandle(HANDLE hFile);
    bool TryServe(HANDLE hFile, LPVOID lpBuffer, DWORD nBytes, LPDWORD lpBytesRead);
}
