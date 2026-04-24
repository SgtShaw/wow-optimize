#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <ctime>
#include "version.h"

#pragma comment(lib, "dbghelp.lib")

extern "C" void Log(const char* fmt, ...);

static LONG WINAPI ExceptionHandler(EXCEPTION_POINTERS* ExceptionInfo) {
    if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT ||
        ExceptionInfo->ExceptionRecord->ExceptionCode == DBG_PRINTEXCEPTION_C) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CreateDirectoryA("Crashes", NULL);

    time_t now = time(nullptr);
    tm tm_buf;
    localtime_s(&tm_buf, &now);
    char filename[MAX_PATH];
    sprintf_s(filename, sizeof(filename), "Crashes\\wow_crash_%04d%02d%02d_%02d%02d%02d.dmp",
              tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
              tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

    HANDLE hFile = CreateFileA(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mdei;
        mdei.ThreadId           = GetCurrentThreadId();
        mdei.ExceptionPointers  = ExceptionInfo;
        mdei.ClientPointers     = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                        (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
                        &mdei, NULL, NULL);
        CloseHandle(hFile);
    }

    Log("CRASH: 0x%08X at 0x%08X. Dump written to %s",
        ExceptionInfo->ExceptionRecord->ExceptionCode,
        (uintptr_t)ExceptionInfo->ExceptionRecord->ExceptionAddress,
        filename);

    return EXCEPTION_CONTINUE_SEARCH;
}

namespace CrashDumper {

bool Init() {
#if TEST_DISABLE_CRASH_DUMPER
    return false;
#else
    AddVectoredExceptionHandler(1, ExceptionHandler);
    Log("[CrashDumper] Vectored exception handler registered");
    return true;
#endif
}

void Shutdown() {
#if !TEST_DISABLE_CRASH_DUMPER
    // Vectored handlers are automatically removed on process exit.
    // No explicit cleanup needed.
#endif
}

} // namespace CrashDumper