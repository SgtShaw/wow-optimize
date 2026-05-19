#pragma once
#include <windows.h>

// Init/shutdown
bool InitAddonPreload();
void ShutdownAddonPreload();
void ClearAddonPreload();

// Called from hooked_CreateFileA/W to track addon file handles
void AddonPreload_OnCreateFile(HANDLE hFile, const char* filename);
void AddonPreload_OnWriteFile(const char* filename);
bool AddonPreload_TryServe(HANDLE hFile, LPVOID lpBuffer, DWORD nBytes, LPDWORD lpBytesRead);
