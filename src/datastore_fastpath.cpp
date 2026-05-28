#include <windows.h>
#include <MinHook.h>
#include <cstdint>
#include "version.h"
#include "datastore_fastpath.h"

extern "C" void Log(const char* fmt, ...);

struct CDataStore {
    void**   vtable;         // +0x00: vtable pointer
    uint8_t* buffer;         // +0x04: data buffer base
    uint32_t base_offset;    // +0x08: subtracted from buffer ptr for effective addr
    uint32_t allocated_size; // +0x0C: allocated capacity in bytes
    uint32_t write_pos;      // +0x10: absolute write position
    uint32_t read_pos;       // +0x14: absolute read position
};

// TLS-cached pointer to avoid repeated buffer arithmetic
struct DataStoreTLS {
    CDataStore* store;
    uint8_t*    effective_base; // buffer - base_offset (precomputed)
    uint32_t    allocated_size;
};
static __declspec(thread) DataStoreTLS t_cache = {};

// ================================================================
// Statistics
// ================================================================
static volatile long g_getdword_calls = 0, g_getdword_hits = 0;
static volatile long g_putdword_calls = 0, g_putdword_hits = 0;
static volatile long g_getbyte_calls  = 0, g_getbyte_hits  = 0;
static volatile long g_putbyte_calls  = 0, g_putbyte_hits  = 0;
static volatile long g_getqword_calls = 0, g_getqword_hits = 0;
static volatile long g_putqword_calls = 0, g_putqword_hits = 0;

// ================================================================
// Original function pointers (__thiscall on x86)
// ================================================================
typedef CDataStore* (__thiscall* GetDword_t)(CDataStore*, uint32_t*);
typedef CDataStore* (__thiscall* PutDword_t)(CDataStore*, uint32_t);
typedef CDataStore* (__thiscall* GetByte_t)(CDataStore*, uint8_t*);
typedef CDataStore* (__thiscall* PutByte_t)(CDataStore*, uint8_t);
typedef CDataStore* (__thiscall* GetQword_t)(CDataStore*, uint32_t*);
typedef CDataStore* (__thiscall* PutQword_t)(CDataStore*, uint32_t, uint32_t);

static GetDword_t pOrigGetDword = nullptr;
static PutDword_t pOrigPutDword = nullptr;
static GetByte_t  pOrigGetByte  = nullptr;
static PutByte_t  pOrigPutByte  = nullptr;
static GetQword_t pOrigGetQword = nullptr;
static PutQword_t pOrigPutQword = nullptr;

// ================================================================
// Inline helpers
// ================================================================
__forceinline void UpdateTLS(CDataStore* s) {
    t_cache.store          = s;
    t_cache.effective_base = s->buffer - s->base_offset;
    t_cache.allocated_size = s->allocated_size;
}

__forceinline bool TLSCacheValid(CDataStore* s) {
    return t_cache.store == s &&
           t_cache.allocated_size == s->allocated_size &&
           t_cache.effective_base == (s->buffer - s->base_offset);
}

// ================================================================
// sub_47B3C0: CDataStore::GetDword (read 4 bytes, 1477 xrefs)
// MinHook __thiscall workaround: use __fastcall, EDX is unused padding
// ================================================================
static CDataStore* __fastcall HookGetDword(CDataStore* self, void*, uint32_t* out) {
#if !TEST_DISABLE_DATASTORE_FASTPATH
    InterlockedIncrement(&g_getdword_calls);

    if (TLSCacheValid(self)) {
        uint32_t rp = self->read_pos;
        if ((rp + 4) <= self->write_pos) {
            InterlockedIncrement(&g_getdword_hits);
            *out = *reinterpret_cast<uint32_t*>(t_cache.effective_base + rp);
            self->read_pos = rp + 4;
            return self;
        }
    }
#endif
    CDataStore* r = pOrigGetDword(self, out);
#if !TEST_DISABLE_DATASTORE_FASTPATH
    UpdateTLS(self);
#endif
    return r;
}

// ================================================================
// sub_47B0A0: CDataStore::PutDword (write 4 bytes, 1185 xrefs)
// ================================================================
static CDataStore* __fastcall HookPutDword(CDataStore* self, void*, uint32_t value) {
#if !TEST_DISABLE_DATASTORE_FASTPATH
    InterlockedIncrement(&g_putdword_calls);

    if (TLSCacheValid(self)) {
        uint32_t wp = self->write_pos;
        uint32_t end = self->base_offset + t_cache.allocated_size;
        if ((wp + 4) <= end) {
            InterlockedIncrement(&g_putdword_hits);
            *reinterpret_cast<uint32_t*>(t_cache.effective_base + wp) = value;
            self->write_pos = wp + 4;
            return self;
        }
    }
#endif
    CDataStore* r = pOrigPutDword(self, value);
#if !TEST_DISABLE_DATASTORE_FASTPATH
    UpdateTLS(self);
#endif
    return r;
}

// ================================================================
// sub_47B340: CDataStore::GetByte (read 1 byte, 504 xrefs)
// ================================================================
static CDataStore* __fastcall HookGetByte(CDataStore* self, void*, uint8_t* out) {
#if !TEST_DISABLE_DATASTORE_FASTPATH
    InterlockedIncrement(&g_getbyte_calls);

    if (TLSCacheValid(self)) {
        uint32_t rp = self->read_pos;
        if ((rp + 1) <= self->write_pos) {
            InterlockedIncrement(&g_getbyte_hits);
            *out = t_cache.effective_base[rp];
            self->read_pos = rp + 1;
            return self;
        }
    }
#endif
    CDataStore* r = pOrigGetByte(self, out);
#if !TEST_DISABLE_DATASTORE_FASTPATH
    UpdateTLS(self);
#endif
    return r;
}

// ================================================================
// sub_47AFE0: CDataStore::PutByte (write 1 byte, 439 xrefs)
// ================================================================
static CDataStore* __fastcall HookPutByte(CDataStore* self, void*, uint8_t value) {
#if !TEST_DISABLE_DATASTORE_FASTPATH
    InterlockedIncrement(&g_putbyte_calls);

    if (TLSCacheValid(self)) {
        uint32_t wp = self->write_pos;
        uint32_t end = self->base_offset + t_cache.allocated_size;
        if ((wp + 1) <= end) {
            InterlockedIncrement(&g_putbyte_hits);
            t_cache.effective_base[wp] = value;
            self->write_pos = wp + 1;
            return self;
        }
    }
#endif
    CDataStore* r = pOrigPutByte(self, value);
#if !TEST_DISABLE_DATASTORE_FASTPATH
    UpdateTLS(self);
#endif
    return r;
}

// ================================================================
// sub_47B100: CDataStore::PutQword (write 8 bytes, 290 xrefs)
// Two stack args: low_dword, high_dword
// ================================================================
static CDataStore* __fastcall HookPutQword(CDataStore* self, void*, uint32_t lo, uint32_t hi) {
#if !TEST_DISABLE_DATASTORE_FASTPATH
    InterlockedIncrement(&g_putqword_calls);

    if (TLSCacheValid(self)) {
        uint32_t wp = self->write_pos;
        uint32_t end = self->base_offset + t_cache.allocated_size;
        if ((wp + 8) <= end) {
            InterlockedIncrement(&g_putqword_hits);
            uint8_t* p = t_cache.effective_base + wp;
            *reinterpret_cast<uint32_t*>(p)     = lo;
            *reinterpret_cast<uint32_t*>(p + 4) = hi;
            self->write_pos = wp + 8;
            return self;
        }
    }
#endif
    CDataStore* r = pOrigPutQword(self, lo, hi);
#if !TEST_DISABLE_DATASTORE_FASTPATH
    UpdateTLS(self);
#endif
    return r;
}

// ================================================================
// sub_47B400: CDataStore::GetQword (read 8 bytes, 284 xrefs)
// Output param: uint32_t* out (IDA: _DWORD* a2, writes 8 bytes)
// ================================================================
static CDataStore* __fastcall HookGetQword(CDataStore* self, void*, uint32_t* out) {
#if !TEST_DISABLE_DATASTORE_FASTPATH
    InterlockedIncrement(&g_getqword_calls);

    if (TLSCacheValid(self)) {
        uint32_t rp = self->read_pos;
        if ((rp + 8) <= self->write_pos) {
            InterlockedIncrement(&g_getqword_hits);
            uint8_t* p = t_cache.effective_base + rp;
            out[0] = *reinterpret_cast<uint32_t*>(p);
            out[1] = *reinterpret_cast<uint32_t*>(p + 4);
            self->read_pos = rp + 8;
            return self;
        }
    }
#endif
    // Original uses uint32_t* out, not uint64_t*
    CDataStore* r = pOrigGetQword(self, out);
#if !TEST_DISABLE_DATASTORE_FASTPATH
    UpdateTLS(self);
#endif
    return r;
}

// ================================================================
// Install hooks
// ================================================================
bool InitDataStoreFastPath() {
#if TEST_DISABLE_DATASTORE_FASTPATH
    Log("[DataStore] DISABLED via feature flag");
    return false;
#else
    struct HookDef {
        void*     addr;
        void*     hook;
        void**    orig;
        const char* name;
        uint32_t  xrefs;
    };

    HookDef hooks[] = {
        { (void*)0x0047B3C0, (void*)HookGetDword,  (void**)&pOrigGetDword,  "GetDword",  1477 },
        { (void*)0x0047B0A0, (void*)HookPutDword,  (void**)&pOrigPutDword,  "PutDword",  1185 },
        { (void*)0x0047B340, (void*)HookGetByte,   (void**)&pOrigGetByte,   "GetByte",    504 },
        { (void*)0x0047AFE0, (void*)HookPutByte,   (void**)&pOrigPutByte,   "PutByte",    439 },
        { (void*)0x0047B100, (void*)HookPutQword,  (void**)&pOrigPutQword,  "PutQword",   290 },
        { (void*)0x0047B400, (void*)HookGetQword,  (void**)&pOrigGetQword,  "GetQword",   284 },
    };

    int installed = 0;
    for (auto& h : hooks) {
        if (WineSafe_CreateHook(h.addr, h.hook, h.orig) == MH_OK) {
            if (MH_EnableHook(h.addr) == MH_OK) {
                installed++;
                Log("[DataStore] Hooked %s at 0x%08X (%d xrefs)", h.name, (DWORD)(uintptr_t)h.addr, h.xrefs);
            }
        }
    }

    Log("[DataStore] Installed %d/%d CDataStore hooks (total %d xrefs)",
        installed, (int)(sizeof(hooks)/sizeof(hooks[0])),
        1477 + 1185 + 504 + 439 + 290 + 284);
    return installed == (int)(sizeof(hooks)/sizeof(hooks[0]));
#endif
}

// ================================================================
// Statistics dump
// ================================================================
void DumpDataStoreStats() {
#if !TEST_DISABLE_DATASTORE_FASTPATH
    Log("[DataStore] === CDataStore Fast Path Statistics ===");
    if (g_getdword_calls > 0)
        Log("[DataStore] GetDword: %ld/%ld (%.1f%%)", g_getdword_hits, g_getdword_calls,
            100.0 * g_getdword_hits / g_getdword_calls);
    if (g_putdword_calls > 0)
        Log("[DataStore] PutDword: %ld/%ld (%.1f%%)", g_putdword_hits, g_putdword_calls,
            100.0 * g_putdword_hits / g_putdword_calls);
    if (g_getbyte_calls > 0)
        Log("[DataStore] GetByte:  %ld/%ld (%.1f%%)", g_getbyte_hits, g_getbyte_calls,
            100.0 * g_getbyte_hits / g_getbyte_calls);
    if (g_putbyte_calls > 0)
        Log("[DataStore] PutByte:  %ld/%ld (%.1f%%)", g_putbyte_hits, g_putbyte_calls,
            100.0 * g_putbyte_hits / g_putbyte_calls);
    if (g_getqword_calls > 0)
        Log("[DataStore] GetQword: %ld/%ld (%.1f%%)", g_getqword_hits, g_getqword_calls,
            100.0 * g_getqword_hits / g_getqword_calls);
    if (g_putqword_calls > 0)
        Log("[DataStore] PutQword: %ld/%ld (%.1f%%)", g_putqword_hits, g_putqword_calls,
            100.0 * g_putqword_hits / g_putqword_calls);
#endif
}

// ================================================================
// Cleanup
// ================================================================
void ShutdownDataStoreFastPath() {
#if !TEST_DISABLE_DATASTORE_FASTPATH
    MH_DisableHook((void*)0x0047B3C0);
    MH_DisableHook((void*)0x0047B0A0);
    MH_DisableHook((void*)0x0047B340);
    MH_DisableHook((void*)0x0047AFE0);
    MH_DisableHook((void*)0x0047B100);
    MH_DisableHook((void*)0x0047B400);
    DumpDataStoreStats();
#endif
}
