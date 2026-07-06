// ============================================================================
// Module: saved_vars_async_serializer.cpp
// Description: Offloads Lua table serialization formatting to background threads.
// Safety & Threading: Clones Lua tables on the main thread, formats off-thread.
// ============================================================================

#include "saved_vars_async_serializer.h"
#include "MinHook.h"
#include "version.h"
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <queue>
#include <fstream>

extern "C" void Log(const char* fmt, ...);

namespace SavedVarsAsyncSerializer {

// Function pointer signatures for Lua API
typedef int (__cdecl *lua_type_fn)(uintptr_t L, int idx);
static const lua_type_fn lua_type_ = (lua_type_fn)0x0084DEB0;

typedef int (__cdecl *lua_toboolean_fn)(uintptr_t L, int idx);
static const lua_toboolean_fn lua_toboolean_ = (lua_toboolean_fn)0x0084E0B0;

typedef double (__cdecl *lua_tonumber_fn)(uintptr_t L, int idx);
static const lua_tonumber_fn lua_tonumber_ = (lua_tonumber_fn)0x0084E030;

typedef const char* (__cdecl *lua_tolstring_fn)(uintptr_t L, int idx, size_t* len);
static const lua_tolstring_fn lua_tolstring_ = (lua_tolstring_fn)0x0084E0E0;

typedef int (__cdecl *lua_next_fn)(uintptr_t L, int idx);
static const lua_next_fn lua_next_ = (lua_next_fn)0x0084EF50;

typedef void (__cdecl *lua_pushnil_fn)(uintptr_t L);
static const lua_pushnil_fn lua_pushnil_ = (lua_pushnil_fn)0x0084E280;

typedef void (__cdecl *lua_settop_fn)(uintptr_t L, int idx);
static const lua_settop_fn lua_settop_ = (lua_settop_fn)0x0084DBF0;

typedef int (__cdecl *lua_gettop_fn)(uintptr_t L);
static const lua_gettop_fn lua_gettop_ = (lua_gettop_fn)0x0084DBD0;

typedef void (__cdecl *lua_getfield_fn)(uintptr_t L, int index, const char* k);
static const lua_getfield_fn lua_getfield_ = (lua_getfield_fn)0x0084E590;

#define LUA_GLOBALSINDEX (-10002)
#define LUA_TNIL 0
#define LUA_TBOOLEAN 1
#define LUA_TNUMBER 3
#define LUA_TSTRING 4
#define LUA_TTABLE 5

// Saved Variable representation
struct SavedVarNode {
    enum Type { TYPE_NIL, TYPE_BOOL, TYPE_NUMBER, TYPE_STRING, TYPE_TABLE };
    Type type = TYPE_NIL;
    bool boolVal = false;
    double numVal = 0.0;
    std::string strVal;

    struct TableEntry {
        std::shared_ptr<SavedVarNode> key;
        std::shared_ptr<SavedVarNode> val;
    };
    std::vector<TableEntry> tableEntries;
};

// Map file descriptors to filenames
static std::unordered_map<int, std::string> g_openFiles;
static std::mutex g_filesMutex;

// Pending variables list per filename
struct PendingFileWrite {
    std::string filename;
    struct VarEntry {
        std::string name;
        std::shared_ptr<SavedVarNode> node;
    };
    std::vector<VarEntry> variables;
};
static std::unordered_map<std::string, PendingFileWrite> g_pendingWrites;
static std::mutex g_pendingMutex;

// Background worker thread state
static std::thread g_workerThread;
static std::queue<PendingFileWrite> g_taskQueue;
static std::mutex g_queueMutex;
static std::condition_variable g_queueCv;
static bool g_shutdown = false;

// Function detours
typedef int (__cdecl *FileOpen_fn)(const char* filename, int access, int share, int create, int flags);
static FileOpen_fn orig_FileOpen = nullptr;

typedef int (__cdecl *SaveVariable_fn)(int fd, const char* varName);
static SaveVariable_fn orig_SaveVariable = nullptr;

typedef int (__cdecl *FileClose_fn)(int fd);
static FileClose_fn orig_FileClose = nullptr;

// Case-insensitive WTF folder check
static bool ContainsWTF(const char* path) {
    if (!path) return false;
    for (const char* p = path; *p; p++) {
        if ((p[0] == 'W' || p[0] == 'w') &&
            (p[1] == 'T' || p[1] == 't') &&
            (p[2] == 'F' || p[2] == 'f')) {
            return true;
        }
    }
    return false;
}

// Helper to push global to Lua stack safely under SEH without object unwinding
static bool TryGetGlobal(uintptr_t L, const char* varName) {
    __try {
        lua_getfield_(L, LUA_GLOBALSINDEX, varName);
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Clone Lua value recursively
static std::shared_ptr<SavedVarNode> CloneLuaValue(uintptr_t L, int idx, int depth) {
    auto node = std::make_shared<SavedVarNode>();
    if (depth > 24) return node; // Recursion depth safety guard (prevent Lua stack overflow)

    int tt = lua_type_(L, idx);
    if (tt == LUA_TNIL) {
        node->type = SavedVarNode::TYPE_NIL;
    } else if (tt == LUA_TBOOLEAN) {
        node->type = SavedVarNode::TYPE_BOOL;
        node->boolVal = (lua_toboolean_(L, idx) != 0);
    } else if (tt == LUA_TNUMBER) {
        node->type = SavedVarNode::TYPE_NUMBER;
        node->numVal = lua_tonumber_(L, idx);
    } else if (tt == LUA_TSTRING) {
        node->type = SavedVarNode::TYPE_STRING;
        size_t len = 0;
        const char* s = lua_tolstring_(L, idx, &len);
        if (s) {
            node->strVal.assign(s, len);
        }
    } else if (tt == LUA_TTABLE) {
        node->type = SavedVarNode::TYPE_TABLE;
        
        int absIdx = idx;
        if (idx < 0 && idx > -10000) {
            absIdx = lua_gettop_(L) + idx + 1; // convert to absolute
        }
        lua_pushnil_(L);
        
        while (lua_next_(L, absIdx) != 0) {
            // Key is at -2, value is at -1
            auto key = CloneLuaValue(L, -2, depth + 1);
            auto val = CloneLuaValue(L, -1, depth + 1);
            node->tableEntries.push_back({key, val});
            // Pop value, keep key for next iteration
            lua_settop_(L, -2);
        }
    }
    return node;
}

// Serialize variable recursively to string
static void SerializeNode(std::string& out, const std::shared_ptr<SavedVarNode>& node, int indent) {
    if (!node) {
        out += "nil";
        return;
    }
    std::string indentStr(indent * 4, ' ');
    if (node->type == SavedVarNode::TYPE_NIL) {
        out += "nil";
    } else if (node->type == SavedVarNode::TYPE_BOOL) {
        out += node->boolVal ? "true" : "false";
    } else if (node->type == SavedVarNode::TYPE_NUMBER) {
        char buf[64];
        sprintf_s(buf, "%.17g", node->numVal);
        out += buf;
    } else if (node->type == SavedVarNode::TYPE_STRING) {
        out += "\"";
        for (char c : node->strVal) {
            if (c == '\\' || c == '"') {
                out += '\\'; out += c;
            } else if (c == '\n') {
                out += "\\n";
            } else if (c == '\r') {
                out += "\\r";
            } else {
                out += c;
            }
        }
        out += "\"";
    } else if (node->type == SavedVarNode::TYPE_TABLE) {
        out += "{\n";
        for (const auto& entry : node->tableEntries) {
            out += indentStr + "    [";
            SerializeNode(out, entry.key, 0);
            out += "] = ";
            SerializeNode(out, entry.val, indent + 1);
            out += ",\n";
        }
        out += indentStr + "}";
    }
}

// Background thread loop
static void WorkerThreadProc() {
    while (true) {
        PendingFileWrite task;
        {
            std::unique_lock<std::mutex> lock(g_queueMutex);
            g_queueCv.wait(lock, [] { return !g_taskQueue.empty() || g_shutdown; });
            if (g_shutdown && g_taskQueue.empty()) break;
            task = std::move(g_taskQueue.front());
            g_taskQueue.pop();
        }
        
        // Write the variables to file
        std::string buffer;
        for (const auto& var : task.variables) {
            buffer += var.name + " = ";
            SerializeNode(buffer, var.node, 0);
            buffer += "\r\n";
        }
        
        // Open file and write the complete buffer in one block (avoiding locking stutters)
        std::ofstream file(task.filename, std::ios::out | std::ios::binary);
        if (file.is_open()) {
            file.write(buffer.data(), buffer.size());
            file.close();
        } else {
            Log("[SavedVarsAsyncSerializer] Failed to open file for write: '%s'", task.filename.c_str());
        }
    }
}

// Hooked File Open
static int __cdecl Hooked_FileOpen(const char* filename, int access, int share, int create, int flags) {
    int fd = orig_FileOpen(filename, access, share, create, flags);
    if (fd != -1 && ContainsWTF(filename)) {
        std::lock_guard<std::mutex> lock(g_filesMutex);
        g_openFiles[fd] = filename;
    }
    return fd;
}

// Hooked Save Variable
static int __cdecl Hooked_SaveVariable(int fd, const char* varName) {
    std::string filename;
    {
        std::lock_guard<std::mutex> lock(g_filesMutex);
        auto it = g_openFiles.find(fd);
        if (it != g_openFiles.end()) {
            filename = it->second;
        }
    }
    
    if (!filename.empty()) {
        uintptr_t L = *(uintptr_t*)0x00D3F78C;
        if (L >= 0x10000 && L < 0xFFE00000) {
            if (TryGetGlobal(L, varName)) {
                // Clone the variable recursively on main thread (very fast)
                auto root = CloneLuaValue(L, -1, 0);
                
                // Pop value from stack
                lua_settop_(L, -2);
                
                // Save it to pending list
                std::lock_guard<std::mutex> lock(g_pendingMutex);
                auto& pending = g_pendingWrites[filename];
                pending.filename = filename;
                pending.variables.push_back({varName, root});
                
                return 1; // Signal success to WoW
            }
        }
    }
    
    return orig_SaveVariable(fd, varName);
}

// Hooked File Close
static int __cdecl Hooked_FileClose(int fd) {
    std::string filename;
    {
        std::lock_guard<std::mutex> lock(g_filesMutex);
        auto it = g_openFiles.find(fd);
        if (it != g_openFiles.end()) {
            filename = it->second;
            g_openFiles.erase(it);
        }
    }
    
    if (!filename.empty()) {
        // Retrieve and hand over task to the background thread
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        auto it = g_pendingWrites.find(filename);
        if (it != g_pendingWrites.end()) {
            {
                std::lock_guard<std::mutex> qLock(g_queueMutex);
                g_taskQueue.push(std::move(it->second));
                g_queueCv.notify_one();
            }
            g_pendingWrites.erase(it);
        }
    }
    
    return orig_FileClose(fd);
}

bool Init() {
#if TEST_DISABLE_SAVEDVARS_ASYNC
    Log("[SavedVarsAsyncSerializer] DISABLED via TEST_DISABLE_SAVEDVARS_ASYNC.");
    return false;
#endif

    g_shutdown = false;
    
    // Spawn worker thread
    g_workerThread = std::thread(WorkerThreadProc);
    
    // Hook target functions
    void* target_FileOpen = (void*)0x00461FA0;
    void* target_SaveVariable = (void*)0x00818B50;
    void* target_FileClose = (void*)0x00461B00;
    
    if (MH_CreateHook(target_FileOpen, (void*)Hooked_FileOpen, (void**)&orig_FileOpen) != MH_OK ||
        MH_CreateHook(target_SaveVariable, (void*)Hooked_SaveVariable, (void**)&orig_SaveVariable) != MH_OK ||
        MH_CreateHook(target_FileClose, (void*)Hooked_FileClose, (void**)&orig_FileClose) != MH_OK) 
    {
        g_shutdown = true;
        g_queueCv.notify_all();
        if (g_workerThread.joinable()) g_workerThread.join();
        Log("[SavedVarsAsyncSerializer] Failed to install detours");
        return false;
    }
    
    MH_EnableHook(target_FileOpen);
    MH_EnableHook(target_SaveVariable);
    MH_EnableHook(target_FileClose);
    
    Log("[SavedVarsAsyncSerializer] Active - Off-thread asynchronous SavedVariables serialization active");
    return true;
}

void Shutdown() {
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        g_shutdown = true;
        g_queueCv.notify_all();
    }
    if (g_workerThread.joinable()) {
        g_workerThread.join();
    }
    
    MH_DisableHook((void*)0x00461FA0);
    MH_DisableHook((void*)0x00818B50);
    MH_DisableHook((void*)0x00461B00);
}

} // namespace SavedVarsAsyncSerializer
