#include "saved_vars_opt.h"
#include <stdio.h>
#include <string.h>

namespace SavedVarsOpt {
    static bool g_enabled = true;

    bool Init() {
        return true;
    }

    void Shutdown() {
        // No-op
    }

    bool OptimizeSerialization(const char* filepath) {
        if (!g_enabled || !filepath) return false;

        // Ensure we only optimize .lua saved variables files
        const char* ext = strrchr(filepath, '.');
        if (!ext || _stricmp(ext, ".lua") != 0) return false;

        // Simple and safe compacting post-process of the written file.
        // It reads the file and strips redundant spaces/newlines where possible
        // to reduce disk space and speed up subsequent loading.
        FILE* infile = nullptr;
        if (fopen_s(&infile, filepath, "rb") != 0 || !infile) return false;

        fseek(infile, 0, SEEK_END);
        long size = ftell(infile);
        fseek(infile, 0, SEEK_SET);

        if (size <= 0 || size > 50 * 1024 * 1024) { // Ignore empty or extremely large files (>50MB) for safety
            fclose(infile);
            return false;
        }

        char* buffer = new char[size + 1];
        size_t readBytes = fread(buffer, 1, size, infile);
        buffer[readBytes] = '\0';
        fclose(infile);

        // We will perform a basic compaction:
        // - Replace multiple spaces with a single space where safe (not inside strings)
        // - Strip out comments (lines starting with --) unless they are within strings
        // - Strip redundant newlines
        char* outBuffer = new char[size + 1];
        size_t outIdx = 0;
        bool inString = false;
        char stringChar = 0;

        for (size_t i = 0; i < readBytes; ++i) {
            char c = buffer[i];

            // Handle string boundaries
            if ((c == '"' || c == '\'') && (i == 0 || buffer[i - 1] != '\\')) {
                if (!inString) {
                    inString = true;
                    stringChar = c;
                } else if (c == stringChar) {
                    inString = false;
                }
            }

            if (!inString) {
                // Strip comments
                if (c == '-' && i + 1 < readBytes && buffer[i + 1] == '-') {
                    // Skip to end of line
                    while (i < readBytes && buffer[i] != '\n' && buffer[i] != '\r') {
                        i++;
                    }
                    continue;
                }

                // Compress multiple whitespaces
                if (c == ' ' || c == '\t') {
                    if (outIdx > 0 && outBuffer[outIdx - 1] == ' ') {
                        continue;
                    }
                    c = ' ';
                }
            }

            outBuffer[outIdx++] = c;
        }
        outBuffer[outIdx] = '\0';

        // Write back if we actually compacted the file
        if (outIdx < readBytes) {
            FILE* outfile = nullptr;
            if (fopen_s(&outfile, filepath, "wb") == 0 && outfile) {
                fwrite(outBuffer, 1, outIdx, outfile);
                fclose(outfile);
            }
        }

        delete[] buffer;
        delete[] outBuffer;
        return true;
    }
}
