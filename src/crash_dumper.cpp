// ================================================================
// crash_dumper.cpp - Top-level exception filter crash reporter
//
// v3.6.5 rewrite: replaced first-chance VEH with
// SetUnhandledExceptionFilter. The old VEH called
// MiniDumpWriteDump for every first-chance exception - including
// handled SEH probes and x87 traps - which on Wine takes the
// loader lock and wedges the process for 60 s per dump.
//
// The unhandled filter runs ONLY on genuine unhandled crashes,
// chains to WoW's own WowError filter, and uses a platform-aware
// dump format:
//   - Wine:    plain-text report (registers + EBP-chain stack +
//              module map). No dbghelp - no loader-lock hazard.
//   - Windows: minidump (MiniDumpNormal, no ScanMemory).
// ================================================================

#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <ctime>
#include "version.h"

#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

extern "C" void Log(const char* fmt, ...);

// Previous unhandled-exception filter (WoW's WowError reporter)
static LPTOP_LEVEL_EXCEPTION_FILTER s_prevFilter = nullptr;

// One-shot: only dump the first crash
static volatile LONG s_dumped = 0;

// ================================================================
// Exception code → human-readable name
// ================================================================
static const char* ExceptionName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:        return "ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:   return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:              return "BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:   return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DENORMAL_OPERAND:    return "FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:      return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INEXACT_RESULT:      return "FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_INVALID_OPERATION:   return "FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW:            return "FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK:         return "FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW:           return "FLT_UNDERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:     return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:           return "IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:      return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:            return "INT_OVERFLOW";
    case EXCEPTION_INVALID_DISPOSITION:     return "INVALID_DISPOSITION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:return "NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_PRIV_INSTRUCTION:        return "PRIVILEGED_INSTRUCTION";
    case EXCEPTION_SINGLE_STEP:             return "SINGLE_STEP";
    case EXCEPTION_STACK_OVERFLOW:          return "STACK_OVERFLOW";
    default:                                return "UNKNOWN";
    }
}

// ================================================================
// EBP-chain stack walk (user-mode, no dbghelp)
// ================================================================
static void WriteStackWalk(HANDLE hFile, CONTEXT* ctx) {
    char buf[128];
    DWORD written;

    const char* hdr = "\n=== STACK TRACE (EBP chain) ===\n";
    WriteFile(hFile, hdr, (DWORD)strlen(hdr), &written, NULL);

    DWORD ebp = ctx->Ebp;
    DWORD eip = ctx->Eip;

    for (int frame = 0; frame < 64; frame++) {
        int len = sprintf_s(buf, sizeof(buf), "  [%02d] EIP=0x%08X  EBP=0x%08X\n", frame, eip, ebp);
        if (len > 0) WriteFile(hFile, buf, (DWORD)len, &written, NULL);

        // Validate EBP before dereferencing
        if (ebp < 0x10000 || (ebp & 3) != 0)
            break;

        __try {
            DWORD nextEbp = *(DWORD*)(ebp);
            DWORD retAddr = *(DWORD*)(ebp + 4);

            // Sanity: EBP must grow up the stack, return addr must be > 0x10000
            if (nextEbp <= ebp || nextEbp == 0 || retAddr < 0x10000)
                break;

            eip = retAddr;
            ebp = nextEbp;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
    }
}

// ================================================================
// Loaded module enumeration (user-mode, no dbghelp)
// ================================================================
static void WriteModuleMap(HANDLE hFile) {
    char buf[256];
    DWORD written;

    const char* hdr = "\n=== LOADED MODULES ===\n";
    WriteFile(hFile, hdr, (DWORD)strlen(hdr), &written, NULL);

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (hSnap == INVALID_HANDLE_VALUE) return;

    MODULEENTRY32 me = { sizeof(me) };
    if (Module32First(hSnap, &me)) {
        do {
            int len = sprintf_s(buf, sizeof(buf), "  0x%08X - 0x%08X  %s\n",
                (uintptr_t)me.modBaseAddr,
                (uintptr_t)me.modBaseAddr + me.modBaseSize,
                me.szModule);
            if (len > 0) WriteFile(hFile, buf, (DWORD)len, &written, NULL);
        } while (Module32Next(hSnap, &me));
    }
    CloseHandle(hSnap);
}

// ================================================================
// Register dump
// ================================================================
static void WriteRegisters(HANDLE hFile, CONTEXT* ctx) {
    char buf[512];
    DWORD written;

    const char* hdr = "\n=== REGISTERS ===\n";
    WriteFile(hFile, hdr, (DWORD)strlen(hdr), &written, NULL);

    int len = sprintf_s(buf, sizeof(buf),
        "  EAX=0x%08X  EBX=0x%08X  ECX=0x%08X  EDX=0x%08X\n"
        "  ESI=0x%08X  EDI=0x%08X  EBP=0x%08X  ESP=0x%08X\n"
        "  EIP=0x%08X  EFL=0x%08X\n",
        ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx,
        ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp,
        ctx->Eip, ctx->EFlags);
    if (len > 0) WriteFile(hFile, buf, (DWORD)len, &written, NULL);
}

// ================================================================
// Wine text-format crash report - no dbghelp, no loader lock
// ================================================================
static void WriteTextReport(EXCEPTION_POINTERS* ep) {
    CreateDirectoryA("Crashes", NULL);

    time_t now = time(nullptr);
    tm tm_buf;
    localtime_s(&tm_buf, &now);
    char filename[MAX_PATH];
    sprintf_s(filename, sizeof(filename), "Crashes\\wow_crash_%04d%02d%02d_%02d%02d%02d.txt",
              tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
              tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

    HANDLE hFile = CreateFileA(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    DWORD written;
    char buf[512];

    DWORD code = ep->ExceptionRecord->ExceptionCode;
    uintptr_t addr = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;

    int hlen = sprintf_s(buf, sizeof(buf),
        "wow_optimize v" WOW_OPTIMIZE_VERSION_STR " crash report (Wine)\n"
        "==================================================\n"
        "Time:       %04d-%02d-%02d %02d:%02d:%02d\n"
        "Process ID: %lu\n"
        "Thread ID:  %lu\n"
        "Exception:  0x%08X (%s) at 0x%08X\n",
        tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
        tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
        GetCurrentProcessId(), GetCurrentThreadId(),
        code, ExceptionName(code), addr);
    if (hlen > 0) WriteFile(hFile, buf, (DWORD)hlen, &written, NULL);

    WriteRegisters(hFile, ep->ContextRecord);
    WriteStackWalk(hFile, ep->ContextRecord);
    WriteModuleMap(hFile);

    CloseHandle(hFile);

    Log("CRASH: 0x%08X (%s) at 0x%08X. Text report: %s",
        code, ExceptionName(code), addr, filename);
}

// ================================================================
// Windows minidump (no ScanMemory to avoid loader-lock slowness)
// ================================================================
static void WriteMinidump(EXCEPTION_POINTERS* ep) {
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
        mdei.ExceptionPointers  = ep;
        mdei.ClientPointers     = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                          MiniDumpWithIndirectlyReferencedMemory,
                          &mdei, NULL, NULL);
        CloseHandle(hFile);
    }

    DWORD code = ep->ExceptionRecord->ExceptionCode;
    uintptr_t addr = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    Log("CRASH: 0x%08X (%s) at 0x%08X. Dump: %s",
        code, ExceptionName(code), addr, filename);
}

// ================================================================
// Top-level unhandled exception filter
// ================================================================
static LONG WINAPI WowOpt_UnhandledExceptionFilter(EXCEPTION_POINTERS* ep) {
    // One-shot: only dump the first unhandled crash
    if (InterlockedCompareExchange(&s_dumped, 1, 0) != 0)
        return s_prevFilter ? s_prevFilter(ep) : EXCEPTION_EXECUTE_HANDLER;

    DWORD code = ep->ExceptionRecord->ExceptionCode;
    Log("CRASH DUMPER: unhandled 0x%08X (%s) at 0x%08X",
        code, ExceptionName(code),
        (uintptr_t)ep->ExceptionRecord->ExceptionAddress);

    if (IsWine()) {
        WriteTextReport(ep);
    } else {
        WriteMinidump(ep);
    }

    // Chain to WoW's own error handler (WowError) if it exists
    if (s_prevFilter)
        return s_prevFilter(ep);

    return EXCEPTION_EXECUTE_HANDLER;
}

// ================================================================
// Public API
// ================================================================
namespace CrashDumper {

bool Init() {
#if TEST_DISABLE_CRASH_DUMPER
    return false;
#else
    s_prevFilter = SetUnhandledExceptionFilter(WowOpt_UnhandledExceptionFilter);
    Log("[CrashDumper] Top-level exception filter registered%s",
        IsWine() ? " (Wine: text reports, no dbghelp)" : "");
    return true;
#endif
}

void Shutdown() {
#if !TEST_DISABLE_CRASH_DUMPER
    // Restore previous filter on clean shutdown
    SetUnhandledExceptionFilter(s_prevFilter);
    s_prevFilter = nullptr;
#endif
}

} // namespace CrashDumper
