#include "combat_log_async.h"
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <fstream>

namespace CombatLogAsync {

static std::queue<std::string> g_logQueue;
static std::mutex g_logMutex;
static std::condition_variable g_logCv;
static std::thread g_workerThread;
static std::atomic<bool> g_shutdown{false};
static std::string g_logFilePath = "Logs\\WoWCombatLog.txt";

void LogWorker() {
    std::ofstream file;
    
    while (!g_shutdown) {
        std::string line;
        {
            std::unique_lock<std::mutex> lock(g_logMutex);
            g_logCv.wait(lock, [] { return !g_logQueue.empty() || g_shutdown; });
            
            if (g_shutdown && g_logQueue.empty()) break;
            
            line = g_logQueue.front();
            g_logQueue.pop();
        }

        if (!line.empty()) {
            if (!file.is_open()) {
                file.open(g_logFilePath, std::ios::out | std::ios::app);
            }
            if (file.is_open()) {
                file << line << "\n";
                // Flush periodically rather than every line
                static int lineCount = 0;
                if (++lineCount % 100 == 0) {
                    file.flush();
                }
            }
        }
    }

    if (file.is_open()) {
        file.flush();
        file.close();
    }
}

bool Init() {
    g_shutdown = false;
    g_workerThread = std::thread(LogWorker);
    return true;
}

void Shutdown() {
    g_shutdown = true;
    g_logCv.notify_all();
    if (g_workerThread.joinable()) {
        g_workerThread.join();
    }
}

void WriteLogAsync(const std::string& line) {
    if (line.empty()) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logQueue.push(line);
    g_logCv.notify_one();
}

} // namespace CombatLogAsync
