// ============================================================================
// Module: crash_dumper.cpp
// Description: Monitors game exception handlers and outputs minidump diagnostic logs on crash.
// Safety & Threading: Safe across all threads. Do not allocate heap memory inside exception callbacks.
// ============================================================================

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <cstdio>
#include <ctime>
#include <cstring>
#include "version.h"
#include "crash_dumper.h"

#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")

extern "C" void Log(const char* fmt, ...);

// Previous unhandled-exception filter (WoW's WowError reporter)
static LPTOP_LEVEL_EXCEPTION_FILTER s_prevFilter = nullptr;

// One-shot: only dump the first crash
static volatile LONG s_dumped = 0;

// ================================================================
// Feature Registry - tracks all active optimizations
// ================================================================
static FeatureState s_features[MAX_TRACKED_FEATURES] = {};
static volatile LONG s_featureCount = 0;
static SRWLOCK s_featureLock = SRWLOCK_INIT;

// ================================================================
// Hook Call Trace - lock-free ring buffer for crash diagnosis
// Records last 256 hook calls so we know WHAT was running at crash
// ================================================================
#define HOOK_TRACE_SIZE 256
#define HOOK_TRACE_MASK (HOOK_TRACE_SIZE - 1)

struct HookTraceEntry {
    const char* hookName;   // Static string - safe in crash context
    uintptr_t   addr;       // Address being hooked or accessed
    DWORD       tick;       // GetTickCount when called
    DWORD       threadId;   // Thread that made the call
};

static HookTraceEntry s_hookTrace[HOOK_TRACE_SIZE] = {};
static volatile LONG s_hookTracePos = 0;  // Monotonic write position

// ================================================================
// Crash-time log flush - force ring buffer to disk before dump
// ================================================================
extern void LogFlushImmediate();  // Defined in dllmain.cpp

// Forward declarations for crash report sections
static void WriteFeatureStates(HANDLE hFile);
static void WriteHookTrace(HANDLE hFile);
static void WriteMemoryInfo(HANDLE hFile);

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
    WriteFeatureStates(hFile);
    WriteHookTrace(hFile);
    WriteMemoryInfo(hFile);
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
// Write Feature States to crash report
// ================================================================
static void WriteFeatureStates(HANDLE hFile) {
    char buf[512];
    DWORD written;

    const char* hdr = "\n=== ACTIVE FEATURES (wow_optimize) ===\n";
    WriteFile(hFile, hdr, (DWORD)strlen(hdr), &written, NULL);

    LONG count = InterlockedCompareExchange(&s_featureCount, 0, 0);
    if (count == 0) {
        const char* none = "  (no features registered)\n";
        WriteFile(hFile, none, (DWORD)strlen(none), &written, NULL);
        return;
    }

    DWORD nowTick = GetTickCount();
    for (int i = 0; i < count && i < MAX_TRACKED_FEATURES; i++) {
        FeatureState& f = s_features[i];
        DWORD ageMs = nowTick - f.lastCallTick;
        int len = sprintf_s(buf, sizeof(buf),
            "  %-28s active=%d calls=%lld errors=%lld lastCall=%ums ago%s%s\n",
            f.name ? f.name : "(null)",
            f.active ? 1 : 0,
            f.callCount,
            f.errorCount,
            f.lastCallTick > 0 ? ageMs : 0,
            f.lastError ? " err=\"" : "",
            f.lastError ? f.lastError : "");
        if (f.lastError) {
            // Append closing quote
            int elen = strlen(buf);
            if (elen < (int)sizeof(buf) - 2) {
                buf[elen] = '"'; buf[elen+1] = '\n'; buf[elen+2] = '\0';
            }
        }
        if (len > 0) WriteFile(hFile, buf, (DWORD)strlen(buf), &written, NULL);
    }
}

// ================================================================
// Write Hook Call Trace to crash report
// ================================================================
static void WriteHookTrace(HANDLE hFile) {
    char buf[256];
    DWORD written;

    const char* hdr = "\n=== HOOK CALL TRACE (last 256 calls) ===\n";
    WriteFile(hFile, hdr, (DWORD)strlen(hdr), &written, NULL);

    LONG pos = InterlockedCompareExchange(&s_hookTracePos, 0, 0);
    if (pos == 0) {
        const char* none = "  (no hook calls recorded)\n";
        WriteFile(hFile, none, (DWORD)strlen(none), &written, NULL);
        return;
    }

    // Show last 64 entries (most recent first)
    int showCount = (pos < 64) ? pos : 64;
    for (int i = 0; i < showCount; i++) {
        int idx = (pos - 1 - i) & HOOK_TRACE_MASK;
        HookTraceEntry& e = s_hookTrace[idx];
        if (!e.hookName) continue;
        int len = sprintf_s(buf, sizeof(buf),
            "  [%02d] TID=%5lu tick=%10lu addr=0x%08X %s\n",
            i, e.threadId, e.tick, (unsigned)e.addr, e.hookName);
        if (len > 0) WriteFile(hFile, buf, (DWORD)strlen(buf), &written, NULL);
    }
}

// ================================================================
// Write Process Memory Info to crash report
// ================================================================
static void WriteMemoryInfo(HANDLE hFile) {
    char buf[512];
    DWORD written;

    const char* hdr = "\n=== MEMORY STATE ===\n";
    WriteFile(hFile, hdr, (DWORD)strlen(hdr), &written, NULL);

    PROCESS_MEMORY_COUNTERS pmc = {};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        int len = sprintf_s(buf, sizeof(buf),
            "  WorkingSet:    %.1f MB\n"
            "  PeakWS:        %.1f MB\n"
            "  PrivateBytes:  %.1f MB\n"
            "  PageFaults:    %lu\n",
            pmc.WorkingSetSize / (1024.0 * 1024.0),
            pmc.PeakWorkingSetSize / (1024.0 * 1024.0),
            pmc.PagefileUsage / (1024.0 * 1024.0),
            pmc.PageFaultCount);
        if (len > 0) WriteFile(hFile, buf, (DWORD)strlen(buf), &written, NULL);
    }

    // VA space fragmentation check
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = 0x10000;
    SIZE_T largestFree = 0, totalFree = 0;
    while (addr < 0x7FFF0000) {
        if (VirtualQuery((void*)addr, &mbi, sizeof(mbi))) {
            if (mbi.State == MEM_FREE) {
                if (mbi.RegionSize > largestFree) largestFree = mbi.RegionSize;
                totalFree += mbi.RegionSize;
            }
            addr += mbi.RegionSize;
            if (mbi.RegionSize == 0) addr += 0x10000;
        } else {
            addr += 0x10000;
        }
    }
    int len = sprintf_s(buf, sizeof(buf),
        "  VA Free:       %.1f MB\n"
        "  VA LargestBlk: %.1f MB%s\n",
        totalFree / (1024.0 * 1024.0),
        largestFree / (1024.0 * 1024.0),
        (largestFree < 64 * 1024 * 1024) ? " WARNING: FRAGMENTED" : "");
    if (len > 0) WriteFile(hFile, buf, (DWORD)strlen(buf), &written, NULL);
}

// ================================================================
// Top-level unhandled exception filter
// ================================================================
static LONG WINAPI WowOpt_UnhandledExceptionFilter(EXCEPTION_POINTERS* ep) {
    // One-shot: only dump the first unhandled crash
    if (InterlockedCompareExchange(&s_dumped, 1, 0) != 0)
        return s_prevFilter ? s_prevFilter(ep) : EXCEPTION_EXECUTE_HANDLER;

    DWORD code = ep->ExceptionRecord->ExceptionCode;
    uintptr_t crashAddr = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;

    // CRITICAL: Flush log ring buffer BEFORE writing crash report
    // Without this, the last N log entries (including the cause) are lost
    LogFlushImmediate();

    Log("!!! CRASH !!! 0x%08X (%s) at 0x%08X TID=%lu",
        code, ExceptionName(code), crashAddr, GetCurrentThreadId());

    // Identify which module the crash address belongs to
    HMODULE hCrashMod = NULL;
    char modName[MAX_PATH] = "unknown";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)crashAddr, &hCrashMod)) {
        GetModuleFileNameA(hCrashMod, modName, MAX_PATH);
    }
    Log("!!! CRASH MODULE: %s (base=0x%08X offset=0x%08X)",
        modName, (uintptr_t)hCrashMod,
        hCrashMod ? (unsigned)(crashAddr - (uintptr_t)hCrashMod) : 0);

    // Log active features summary
    LONG fcount = InterlockedCompareExchange(&s_featureCount, 0, 0);
    int activeFeatures = 0;
    for (int i = 0; i < fcount && i < MAX_TRACKED_FEATURES; i++) {
        if (s_features[i].active) activeFeatures++;
    }
    Log("!!! ACTIVE FEATURES: %d/%ld registered", activeFeatures, fcount);

    // Log last few hook calls
    LONG hpos = InterlockedCompareExchange(&s_hookTracePos, 0, 0);
    for (int i = 0; i < 5 && i < hpos; i++) {
        int idx = (hpos - 1 - i) & HOOK_TRACE_MASK;
        HookTraceEntry& e = s_hookTrace[idx];
        if (e.hookName) {
            Log("!!! LAST HOOK[%d]: %s @ 0x%08X (TID=%lu, %ums ago)",
                i, e.hookName, (unsigned)e.addr, e.threadId,
                GetTickCount() - e.tick);
        }
    }

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
    memset(s_features, 0, sizeof(s_features));
    memset(s_hookTrace, 0, sizeof(s_hookTrace));
    InterlockedExchange(&s_featureCount, 0);
    InterlockedExchange(&s_hookTracePos, 0);

    s_prevFilter = SetUnhandledExceptionFilter(WowOpt_UnhandledExceptionFilter);
    Log("[CrashDumper] Enhanced crash reporter active%s",
        IsWine() ? " (Wine: text reports)" : " (Windows: minidump)");
    Log("[CrashDumper] Feature tracking: %d slots, Hook trace: %d entries",
        MAX_TRACKED_FEATURES, HOOK_TRACE_SIZE);
    return true;
#endif
}

void Shutdown() {
#if !TEST_DISABLE_CRASH_DUMPER
    SetUnhandledExceptionFilter(s_prevFilter);
    s_prevFilter = nullptr;
#endif
}

void RegisterFeature(const char* name) {
    AcquireSRWLockExclusive(&s_featureLock);
    LONG idx = InterlockedIncrement(&s_featureCount) - 1;
    if (idx < MAX_TRACKED_FEATURES) {
        s_features[idx].name = name;
        s_features[idx].active = true;
        s_features[idx].callCount = 0;
        s_features[idx].errorCount = 0;
        s_features[idx].lastCallTick = 0;
        s_features[idx].lastError = nullptr;
    }
    ReleaseSRWLockExclusive(&s_featureLock);
}

void FeatureCall(const char* name) {
    // Lock-free fast path: linear scan is fine for <64 features
    // Called from hot paths so must be minimal overhead
    LONG count = InterlockedCompareExchange(&s_featureCount, 0, 0);
    for (int i = 0; i < count && i < MAX_TRACKED_FEATURES; i++) {
        if (s_features[i].name == name || 
            (s_features[i].name && name && strcmp(s_features[i].name, name) == 0)) {
            InterlockedIncrement64((volatile LONG64*)&s_features[i].callCount);
            s_features[i].lastCallTick = GetTickCount();
            return;
        }
    }
}

void FeatureError(const char* name, const char* desc) {
    LONG count = InterlockedCompareExchange(&s_featureCount, 0, 0);
    for (int i = 0; i < count && i < MAX_TRACKED_FEATURES; i++) {
        if (s_features[i].name == name ||
            (s_features[i].name && name && strcmp(s_features[i].name, name) == 0)) {
            InterlockedIncrement64((volatile LONG64*)&s_features[i].errorCount);
            s_features[i].lastError = desc;
            s_features[i].lastCallTick = GetTickCount();
            return;
        }
    }
}

void FeatureSetActive(const char* name, bool active) {
    LONG count = InterlockedCompareExchange(&s_featureCount, 0, 0);
    for (int i = 0; i < count && i < MAX_TRACKED_FEATURES; i++) {
        if (s_features[i].name == name ||
            (s_features[i].name && name && strcmp(s_features[i].name, name) == 0)) {
            s_features[i].active = active;
            return;
        }
    }
}

int GetFeatureStates(FeatureState* out, int maxCount) {
    LONG count = InterlockedCompareExchange(&s_featureCount, 0, 0);
    int copied = 0;
    for (int i = 0; i < count && i < MAX_TRACKED_FEATURES && copied < maxCount; i++) {
        out[copied++] = s_features[i];
    }
    return copied;
}

void RecordHookCall(const char* hookName, uintptr_t addr) {
    // Lock-free: atomic increment gives us a unique slot
    LONG pos = InterlockedIncrement(&s_hookTracePos) - 1;
    int idx = pos & HOOK_TRACE_MASK;
    s_hookTrace[idx].hookName = hookName;
    s_hookTrace[idx].addr = addr;
    s_hookTrace[idx].tick = GetTickCount();
    s_hookTrace[idx].threadId = GetCurrentThreadId();
}

} // namespace CrashDumper

// Called from lua_error_diag to dump hook trace on Lua errors
void CrashDumper_DumpHookTrace(int count) {
    if (count > HOOK_TRACE_SIZE) count = HOOK_TRACE_SIZE;
    LONG hpos = InterlockedCompareExchange(&s_hookTracePos, 0, 0);
    DWORD now = GetTickCount();
    int printed = 0;
    for (int i = 0; i < count && i < hpos; i++) {
        int idx = (hpos - 1 - i) & HOOK_TRACE_MASK;
        HookTraceEntry& e = s_hookTrace[idx];
        if (e.hookName && e.hookName[0]) {
            Log("    [%d] %s @ 0x%08X  TID=%lu  %ums ago",
                i, e.hookName, (unsigned)e.addr, e.threadId,
                (unsigned)(now - e.tick));
            printed++;
        }
    }
    if (printed == 0) {
        Log("    (no hook trace entries recorded)");
    }
}
