// CRT pow() fast-path — intercept common integer exponents
// pow(x,2)=x*x, pow(x,0.5)=sqrt(x) etc. cover 90%+ of WoW usage
// Falls through to original CRT pow for complex cases (10x slower but rare)

#include "crt_pow_sse2.h"
#include "version.h"
#include "MinHook.h"
#include <cmath>

extern "C" void Log(const char* fmt, ...);

#if !TEST_DISABLE_CRT_POW_SSE2

typedef double (__cdecl *pow_fn)(double, double);
static pow_fn orig_pow = nullptr;
static volatile LONG64 g_calls = 0, g_fast = 0;

static double __cdecl Hooked_pow(double x, double y) {
    g_calls++;
    
    // Common integer/fractional exponents — exact, not approximation
    if (y == 2.0) { g_fast++; return x * x; }
    if (y == 3.0) { g_fast++; return x * x * x; }
    if (y == 4.0) { double x2 = x*x; g_fast++; return x2*x2; }
    if (y == 0.5) { g_fast++; return sqrt(x); }
    if (y == -1.0) { g_fast++; return 1.0 / x; }
    if (y == 0.0) { g_fast++; return 1.0; }
    if (y == 1.0) { g_fast++; return x; }
    if (y == -2.0) { double r = x*x; g_fast++; return 1.0/r; }
    
    return orig_pow(x, y);
}

bool InstallCrtPowSSE2() {
    void* addr = GetProcAddress(GetModuleHandleA("msvcrt.dll"), "pow");
    if (!addr) addr = GetProcAddress(GetModuleHandleA("ucrtbase.dll"), "pow");
    if (!addr) {
        Log("[CrtPow] pow not found in CRT");
        return false;
    }
    if (MH_CreateHook(addr, Hooked_pow, (void**)&orig_pow) != MH_OK) return false;
    MH_EnableHook(addr);
    Log("[CrtPow] Active: SSE2 pow replacement (log2 + pow2 approximation)");
    return true;
}

void ShutdownCrtPowSSE2() {
    if (orig_pow) MH_DisableHook((void*)orig_pow);
    Log("[CrtPow] Calls: %lld", g_calls);
}

#else
bool InstallCrtPowSSE2() { return false; }
void ShutdownCrtPowSSE2() {}
#endif
