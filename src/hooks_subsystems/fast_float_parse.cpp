#include "fast_float_parse.h"
#include <cstdlib>

namespace FastFloatParse {
    static bool g_enabled = true;

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    float FastAtof(const char* str) {
        if (!str) return 0.0f;
        if (!g_enabled) return (float)std::atof(str);

        // High-speed lock-free float parsing
        float val = 0.0f;
        int sign = 1;
        const char* p = str;

        // Skip leading whitespace
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

        if (*p == '-') {
            sign = -1;
            p++;
        } else if (*p == '+') {
            p++;
        }

        // Integer part
        while (*p >= '0' && *p <= '9') {
            val = val * 10.0f + (*p - '0');
            p++;
        }

        // Fractional part
        if (*p == '.') {
            p++;
            float dec = 0.1f;
            while (*p >= '0' && *p <= '9') {
                val += (*p - '0') * dec;
                dec *= 0.1f;
                p++;
            }
        }

        // Exponent part
        if (*p == 'e' || *p == 'E') {
            p++;
            int expSign = 1;
            if (*p == '-') {
                expSign = -1;
                p++;
            } else if (*p == '+') {
                p++;
            }
            int expVal = 0;
            while (*p >= '0' && *p <= '9') {
                expVal = expVal * 10 + (*p - '0');
                p++;
            }
            if (expVal > 0) {
                float mult = 1.0f;
                for (int i = 0; i < expVal; i++) mult *= 10.0f;
                if (expSign == -1) val /= mult;
                else val *= mult;
            }
        }

        return val * sign;
    }
}
