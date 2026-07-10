#include "packet_processing_throttle.h"
#include <mutex>
#include <unordered_map>

namespace PacketProcessingThrottle {
    static bool g_enabled = true;
    static std::mutex g_throttleMutex;
    static std::unordered_map<unsigned int, DWORD> g_lastProcessedTime;

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    bool ShouldThrottlePacket(unsigned int opcode) {
        if (!g_enabled) return false;

        // Opcode values for WotLK 3.3.5a:
        // SMSG_GUILD_ROSTER = 0x008A
        // SMSG_GUILD_EVENT_LOG = 0x03C4
        // SMSG_CONTACT_LIST = 0x0096
        // SMSG_MOTD = 0x009E
        if (opcode == 0x008A || opcode == 0x03C4 || opcode == 0x0096 || opcode == 0x009E) {
            DWORD now = GetTickCount();
            std::lock_guard<std::mutex> lock(g_throttleMutex);
            auto it = g_lastProcessedTime.find(opcode);
            if (it != g_lastProcessedTime.end()) {
                if (now - it->second < 10000) { // Limit to once every 10 seconds in combat / heavy load
                    return true; 
                }
            }
            g_lastProcessedTime[opcode] = now;
        }
        return false;
    }
}
