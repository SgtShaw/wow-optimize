#include "combat_log_filter.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

namespace CombatLogFilter {
    static bool g_enabled = true;
    static unsigned int g_filteredCount = 0;

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    bool ShouldFilterEvent(int eventId, const char* format, va_list args) {
        if (!g_enabled) return false;

        // We only filter if the format string looks like a standard combat log event
        // In 3.3.5a: "d s d s i d s i ..." or similar (timestamp, event, sourceGUID, sourceName, sourceFlags, ...)
        if (format && strlen(format) >= 8) {
            // Check if format begins with 'd' (timestamp), 's' (event type)
            if (format[0] == 'd' && format[1] == 's') {
                va_list argsCopy;
                va_copy(argsCopy, args);

                __try {
                    // 1. Timestamp (double)
                    va_arg(argsCopy, double);
                    // 2. Event type (string)
                    va_arg(argsCopy, const char*);
                    
                    // 3. Source GUID (could be double/uint64 or string/hex depending on context)
                    if (format[2] == 'd') {
                        va_arg(argsCopy, double);
                    } else if (format[2] == 's') {
                        va_arg(argsCopy, const char*);
                    } else {
                        va_arg(argsCopy, unsigned __int64);
                    }

                    // 4. Source Name (string)
                    va_arg(argsCopy, const char*);

                    // 5. Source Flags (int)
                    int sourceFlags = va_arg(argsCopy, int);

                    // 6. Dest GUID
                    if (format[5] == 'd') {
                        va_arg(argsCopy, double);
                    } else if (format[5] == 's') {
                        va_arg(argsCopy, const char*);
                    } else {
                        va_arg(argsCopy, unsigned __int64);
                    }

                    // 7. Dest Name (string)
                    va_arg(argsCopy, const char*);

                    // 8. Dest Flags (int)
                    int destFlags = va_arg(argsCopy, int);

                    // Check if either belongs to player, party, or raid
                    // COMBATLOG_OBJECT_AFFILIATION_MINE      = 0x00000001
                    // COMBATLOG_OBJECT_AFFILIATION_PARTY     = 0x00000002
                    // COMBATLOG_OBJECT_AFFILIATION_RAID      = 0x00000004
                    bool sourceIsRelevant = (sourceFlags & 0x00000007) != 0;
                    bool destIsRelevant = (destFlags & 0x00000007) != 0;

                    if (!sourceIsRelevant && !destIsRelevant) {
                        g_filteredCount++;
                        va_end(argsCopy);
                        return true; // Filter this event!
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    // Safe fallback: do not filter on error
                }
                va_end(argsCopy);
            }
        }

        return false;
    }
}
