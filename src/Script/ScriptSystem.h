#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <filesystem>
#include <chrono>

struct lua_State;

namespace EngineScript {

struct ScriptContext {
    std::string   filePath;
    std::string   name;
    lua_State*    L = nullptr;
    bool          loaded = false;
    bool          hasError = false;
    std::string   lastError;
    std::filesystem::file_time_type lastWriteTime;
    ScriptContext() = default;
    ScriptContext(const ScriptContext&) = delete;
    ScriptContext& operator=(const ScriptContext&) = delete;
};

class ScriptSystem {
public:
    static ScriptSystem& Get();

    bool Initialize();
    void Shutdown();
    void Update(float dt);

    uint32_t LoadScript(const std::string& filePath);
    void     UnloadScript(uint32_t id);
    void     ReloadScript(uint32_t id);
    void     ReloadAll();

    void CallOnStart(uint32_t id);
    void CallOnUpdate(uint32_t id, float dt);
    void CallOnEvent(uint32_t id, const std::string& eventName, const std::string& eventData = "");

    bool ExecString(uint32_t id, const std::string& code, std::string& outError);

    void   SetGlobalNumber(uint32_t id, const std::string& name, double value);
    void   SetGlobalString(uint32_t id, const std::string& name, const std::string& value);
    double GetGlobalNumber(uint32_t id, const std::string& name, double defaultVal = 0.0);

    const ScriptContext* GetContext(uint32_t id) const;
    size_t GetScriptCount() const { return m_scripts.size(); }

    void EnableHotReload(bool enable) { m_hotReloadEnabled = enable; }
    bool IsHotReloadEnabled() const   { return m_hotReloadEnabled; }

    void RegisterCallback(const std::string& name, std::function<std::string(const std::string&)> cb);

    void DrawUI();
    bool IsUIOpen() const     { return m_uiOpen; }
    void SetUIOpen(bool open) { m_uiOpen = open; }
    void ToggleUI()           { m_uiOpen = !m_uiOpen; }

    void LogConsole(const std::string& msg) {
        m_consoleLog.push_back(msg);
        if (m_consoleLog.size() > 500) {
            m_consoleLog.erase(m_consoleLog.begin());
        }
    }

    std::string InvokeCallback(const std::string& name, const std::string& arg) {
        auto it = m_callbacks.find(name);
        if (it != m_callbacks.end()) {
            return it->second(arg);
        }
        return "ERROR: callback not found: " + name;
    }

private:
    ScriptSystem() = default;
    ~ScriptSystem();
    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;

    void RegisterEngineAPI(lua_State* L);
    bool LoadFile(ScriptContext& ctx);
    bool SafeCall(lua_State* L, int nargs, int nresults, std::string& outError);

    std::unordered_map<uint32_t, std::unique_ptr<ScriptContext>> m_scripts;
    std::unordered_map<std::string, std::function<std::string(const std::string&)>> m_callbacks;
    uint32_t m_nextId = 1;

    bool  m_initialized       = false;
    bool  m_hotReloadEnabled  = true;
    bool  m_uiOpen            = false;
    float m_hotReloadTimer    = 0.0f;
    float m_hotReloadInterval = 1.0f;

    char m_consoleInput[1024] = {};
    std::vector<std::string> m_consoleLog;
    uint32_t m_selectedScriptId = 0;
};

} // namespace EngineScript
