#include "net_addon_coalescer.h"
#include "MinHook.h"
#include "version.h"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

extern "C" void Log(const char* fmt, ...);

namespace NetAddonCoalescer {

typedef int (__cdecl *SendAddonMessage_fn)(const char* prefix, const char* text, int channel, const char* target);
static SendAddonMessage_fn orig_SendAddonMessage = nullptr;

struct QueuedMessage {
    std::string prefix;
    std::string text;
    int channel;
    std::string target;
    bool hasTarget;
};

static std::vector<QueuedMessage> g_queue;
static std::mutex g_queueMutex;
static DWORD g_lastFlushTick = 0;

static int __cdecl Hooked_SendAddonMessage(const char* prefix, const char* text, int channel, const char* target) {
    #if !TEST_DISABLE_NET_PACKET_COALESCE
    if (prefix && text) {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        QueuedMessage msg;
        msg.prefix = prefix;
        msg.text = text;
        msg.channel = channel;
        if (target) {
            msg.target = target;
            msg.hasTarget = true;
        } else {
            msg.hasTarget = false;
        }
        g_queue.push_back(msg);
        return 1; // Success
    }
    #endif
    return orig_SendAddonMessage(prefix, text, channel, target);
}

void OnFrame() {
    #if !TEST_DISABLE_NET_PACKET_COALESCE
    DWORD now = GetTickCount();
    if (now - g_lastFlushTick < 50) return; // 20Hz flush rate (50ms)
    
    std::vector<QueuedMessage> toSend;
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        if (g_queue.empty()) return;
        toSend.swap(g_queue);
    }
    
    g_lastFlushTick = now;
    
    for (const auto& msg : toSend) {
        orig_SendAddonMessage(
            msg.prefix.c_str(),
            msg.text.c_str(),
            msg.channel,
            msg.hasTarget ? msg.target.c_str() : nullptr
        );
    }
    #endif
}

bool Init() {
    void* target = (void*)0x0052F1E0;
    
    unsigned char prologue[3];
    __try {
        memcpy(prologue, target, 3);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("[NetAddonCoalescer] Target 0x0052F1E0 not readable.");
        return true;
    }

    if (prologue[0] != 0x55 || prologue[1] != 0x8B || prologue[2] != 0xEC) {
        Log("[NetAddonCoalescer] BAD PROLOGUE at 0x0052F1E0. Skipping hook.");
        return true;
    }

    if (MH_CreateHook(target, (void*)Hooked_SendAddonMessage, (void**)&orig_SendAddonMessage) == MH_OK) {
        if (MH_EnableHook(target) == MH_OK) {
            Log("[NetAddonCoalescer] Active - Addon network messages are coalesced to 20Hz.");
            return true;
        }
        MH_RemoveHook(target);
    }
    return false;
}

void Shutdown() {
    MH_DisableHook((void*)0x0052F1E0);
}

} // namespace NetAddonCoalescer
