#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <cstdint>

class Camera;
class Renderer;

namespace EngineDebug {

enum class LogLevel {
    Info,
    Warning,
    Error,
    Success,
    Command
};

struct ConsoleMessage {
    std::string text;
    LogLevel level = LogLevel::Info;
};

struct ConsoleCommand {
    std::string name;
    std::string description;
    std::string usage;
    std::function<void(const std::vector<std::string>& args)> callback;
};

class DebugConsole {
public:
    static DebugConsole& Get();

    bool Initialize(Renderer* renderer = nullptr, Camera* camera = nullptr);
    void Shutdown();

    // Logging
    void Log(const std::string& text, LogLevel level = LogLevel::Info);
    void LogInfo(const std::string& text)    { Log(text, LogLevel::Info); }
    void LogWarning(const std::string& text) { Log(text, LogLevel::Warning); }
    void LogError(const std::string& text)   { Log(text, LogLevel::Error); }
    void LogSuccess(const std::string& text) { Log(text, LogLevel::Success); }

    // Execution
    void Execute(const std::string& commandLine);
    void RegisterCommand(const std::string& name, const std::string& desc, const std::string& usage,
                         std::function<void(const std::vector<std::string>& args)> callback);

    // References for commands
    void SetRenderer(Renderer* renderer) { m_renderer = renderer; }
    void SetCamera(Camera* camera) { m_camera = camera; }

    // Game speed / time scale
    float GetTimeScale() const { return m_timeScale; }
    void SetTimeScale(float scale) { m_timeScale = (std::max)(0.0f, scale); }

    // HUD FPS display toggle
    bool IsFpsOverlayEnabled() const { return m_showFpsOverlay; }
    void SetFpsOverlayEnabled(bool enable) { m_showFpsOverlay = enable; }

    // UI
    void DrawUI();
    void ToggleConsole() { m_isOpen = !m_isOpen; m_reclaimFocus = true; }
    void SetOpen(bool open) { m_isOpen = open; if (open) m_reclaimFocus = true; }
    bool IsOpen() const { return m_isOpen; }

    void Clear();

private:
    DebugConsole() = default;
    ~DebugConsole() = default;
    DebugConsole(const DebugConsole&) = delete;
    DebugConsole& operator=(const DebugConsole&) = delete;

    std::vector<ConsoleMessage> m_messages;
    std::vector<std::string> m_history;
    int m_historyPos = -1;

    std::unordered_map<std::string, ConsoleCommand> m_commands;
    mutable std::recursive_mutex m_mutex;

    char m_inputBuf[512] = { 0 };
    bool m_isOpen = false;
    bool m_scrollToBottom = true;
    bool m_reclaimFocus = false;
    bool m_showFpsOverlay = false;
    float m_timeScale = 1.0f;

    Renderer* m_renderer = nullptr;
    Camera* m_camera = nullptr;

    void RegisterBuiltinCommands();

public:
    const std::vector<std::string>& GetHistory() const { return m_history; }
    int& GetHistoryPos() { return m_historyPos; }
};

} // namespace EngineDebug
