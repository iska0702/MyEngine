#include "DebugConsole.h"
#include "Profiler.h"
#include "Camera.h"
#include "Renderer.h"
#include "Prefab/PrefabSystem.h"
#include "Script/ScriptSystem.h"
#include "VFX/ParticleSystem.h"
#include "PostProcess/PostProcessSystem.h"
#include "AI/NavigationSystem.h"
#include "Rain/RainSystem.h"

#include <iostream>
#include <sstream>
#include <algorithm>

#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
#include <imgui.h>
#endif

namespace EngineDebug {

DebugConsole& DebugConsole::Get() {
    static DebugConsole instance;
    return instance;
}

bool DebugConsole::Initialize(Renderer* renderer, Camera* camera) {
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_renderer = renderer;
        m_camera = camera;
    }

    RegisterBuiltinCommands();

    LogSuccess("==================================================");
    LogSuccess(" MyEngine Developer Console v2.0 Initialized");
    LogInfo(" Type 'help' or '?' for a list of commands.");
    LogSuccess("==================================================");
    return true;
}

void DebugConsole::Shutdown() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_messages.clear();
    m_history.clear();
    m_commands.clear();
}

void DebugConsole::Clear() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_messages.clear();
}

void DebugConsole::Log(const std::string& text, LogLevel level) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_messages.push_back({ text, level });
    if (m_messages.size() > 2000) {
        m_messages.erase(m_messages.begin(), m_messages.begin() + 200);
    }
    m_scrollToBottom = true;
}

void DebugConsole::RegisterCommand(const std::string& name, const std::string& desc, const std::string& usage,
                                   std::function<void(const std::vector<std::string>& args)> callback) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_commands[name] = { name, desc, usage, callback };
}

static std::vector<std::string> Tokenize(const std::string& str) {
    std::vector<std::string> tokens;
    std::string token;
    bool inQuotes = false;

    for (size_t i = 0; i < str.length(); ++i) {
        char c = str[i];
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ' ' && !inQuotes) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
            token += c;
        }
    }
    if (!token.empty()) tokens.push_back(token);
    return tokens;
}

void DebugConsole::Execute(const std::string& commandLine) {
    std::string cmd = commandLine;
    size_t start = cmd.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return;
    cmd = cmd.substr(start);

    Log("> " + cmd, LogLevel::Command);

    // Save to history
    m_history.push_back(cmd);
    m_historyPos = -1;

    auto tokens = Tokenize(cmd);
    if (tokens.empty()) return;

    std::string cmdName = tokens[0];
    std::transform(cmdName.begin(), cmdName.end(), cmdName.begin(), ::tolower);
    tokens.erase(tokens.begin());

    auto it = m_commands.find(cmdName);
    if (it != m_commands.end()) {
        try {
            it->second.callback(tokens);
        } catch (const std::exception& ex) {
            LogError("Exception while executing '" + cmdName + "': " + ex.what());
        }
    } else {
        LogError("Unknown command: '" + cmdName + "'. Type 'help' for available commands.");
    }
}

void DebugConsole::RegisterBuiltinCommands() {
    // 1. help
    RegisterCommand("help", "List all commands or get help on a specific command", "help [cmd]", [this](const auto& args) {
        if (args.empty()) {
            LogInfo("Available Console Commands:");
            for (const auto& pair : m_commands) {
                Log("  " + pair.first + " - " + pair.second.description, LogLevel::Info);
            }
        } else {
            auto it = m_commands.find(args[0]);
            if (it != m_commands.end()) {
                LogInfo("Command: " + it->second.name);
                LogInfo("Description: " + it->second.description);
                LogInfo("Usage: " + it->second.usage);
            } else {
                LogWarning("Command not found: " + args[0]);
            }
        }
    });
    m_commands["?"] = m_commands["help"];

    // 2. clear / cls
    RegisterCommand("clear", "Clears the console log", "clear", [this](const std::vector<std::string>&) { Clear(); });
    m_commands["cls"] = m_commands["clear"];

    // 3. fps
    RegisterCommand("fps", "Toggle on-screen FPS counter", "fps [0|1]", [this](const auto& args) {
        if (args.empty()) {
            m_showFpsOverlay = !m_showFpsOverlay;
        } else {
            m_showFpsOverlay = (args[0] == "1" || args[0] == "true" || args[0] == "on");
        }
        LogInfo(std::string("FPS Overlay: ") + (m_showFpsOverlay ? "ON" : "OFF"));
    });

    // 4. stat
    RegisterCommand("stat", "Display engine stats & profiler data", "stat [fps|gpu|scene]", [this](const auto& args) {
        float fps = Profiler::Get().GetCurrentFPS();
        float ft = Profiler::Get().GetCurrentFrameTimeMs();
        float avgFps = Profiler::Get().GetAverageFPS();
        float p1Low = Profiler::Get().GetOnePercentLowFPS();

        std::ostringstream ss;
        ss << "[Telemetry] FPS: " << fps << " | FrameTime: " << ft << "ms | Avg: " << avgFps
           << " | 1% Low: " << p1Low;
        LogInfo(ss.str());

        const auto& gpu = Profiler::Get().GetGpuTelemetry();
        std::ostringstream ss2;
        ss2 << "[GPU] DrawCalls: " << gpu.drawCalls << " | PointLights: " << gpu.pointLights
            << " | SpotLights: " << gpu.spotLights << " | Particles: " << gpu.activeParticles;
        LogInfo(ss2.str());
    });

    // 5. tp
    RegisterCommand("tp", "Teleport camera to coordinates", "tp <x> <y> <z>", [this](const auto& args) {
        if (args.size() < 3 || !m_camera) {
            LogWarning("Usage: tp <x> <y> <z>");
            return;
        }
        float x = std::stof(args[0]);
        float y = std::stof(args[1]);
        float z = std::stof(args[2]);
        m_camera->SetPosition(XMFLOAT3(x, y, z));
        LogSuccess("Teleported camera to (" + args[0] + ", " + args[1] + ", " + args[2] + ")");
    });

    // 6. time_scale
    RegisterCommand("time_scale", "Set simulation time scale", "time_scale <scale>", [this](const auto& args) {
        if (args.empty()) {
            LogInfo("Current time scale: " + std::to_string(m_timeScale));
            return;
        }
        float s = std::stof(args[0]);
        SetTimeScale(s);
        LogSuccess("Time scale set to: " + std::to_string(m_timeScale));
    });

    // 7. freecam
    RegisterCommand("freecam", "Toggle camera freecam mode", "freecam [0|1]", [this](const std::vector<std::string>& args) {
        if (!m_camera) return;
        bool enable = args.empty() ? !m_camera->IsFreecamMode() : (args[0] == "1" || args[0] == "true");
        m_camera->SetFreecamMode(enable);
        LogSuccess(std::string("Freecam: ") + (enable ? "ENABLED" : "DISABLED"));
    });

    // 8. spawn (prefab)
    RegisterCommand("spawn", "Instantiate a prefab archetype at camera position", "spawn <prefab_name>", [this](const std::vector<std::string>& args) {
        if (args.empty() || !m_camera) {
            LogWarning("Usage: spawn <StreetLamp|Campfire|LootChest|BeaconTower>");
            return;
        }
        XMFLOAT3 pos = m_camera->GetPosition();
        XMFLOAT3 fwd = m_camera->GetForwardVector();
        XMFLOAT3 spawnPos = { pos.x + fwd.x * 3.0f, pos.y + fwd.y * 3.0f, pos.z + fwd.z * 3.0f };

        auto node = EnginePrefab::PrefabSystem::Get().InstantiateWithRenderer(args[0], m_renderer, nullptr, spawnPos);
        if (node) {
            LogSuccess("Spawned prefab '" + args[0] + "' at (" + std::to_string(spawnPos.x) + ", " + std::to_string(spawnPos.y) + ", " + std::to_string(spawnPos.z) + ")");
        } else {
            LogError("Failed to instantiate prefab: " + args[0]);
        }
    });

    // 9. postprocess
    RegisterCommand("postprocess", "Apply post-processing preset", "postprocess <cinematic|horror|cyberpunk|retro|neutral>", [](const std::vector<std::string>& args) {
        if (args.empty()) return;
        std::string p = args[0];
        if (p == "cinematic") EnginePostProcess::PostProcessSystem::Get().ApplyPresetCinematic();
        else if (p == "horror") EnginePostProcess::PostProcessSystem::Get().ApplyPresetHorror();
        else if (p == "cyberpunk") EnginePostProcess::PostProcessSystem::Get().ApplyPresetCyberpunk();
        else if (p == "retro") EnginePostProcess::PostProcessSystem::Get().ApplyPresetRetroVHS();
        else if (p == "neutral") EnginePostProcess::PostProcessSystem::Get().ApplyPresetCleanNeutral();
    });

    // 10. vfx
    RegisterCommand("vfx", "Trigger a particle effect at camera", "vfx <explosion|fire|sparks|smoke|magic>", [this](const std::vector<std::string>& args) {
        if (args.empty() || !m_camera) return;
        XMFLOAT3 pos = m_camera->GetPosition();
        XMFLOAT3 fwd = m_camera->GetForwardVector();
        XMFLOAT3 spawnPos = { pos.x + fwd.x * 4.0f, pos.y + fwd.y * 4.0f, pos.z + fwd.z * 4.0f };

        std::string v = args[0];
        if (v == "explosion") EngineVFX::ParticleSystem::Get().SpawnExplosion(spawnPos);
        else if (v == "fire") EngineVFX::ParticleSystem::Get().SpawnFire(spawnPos);
        else if (v == "sparks") EngineVFX::ParticleSystem::Get().SpawnSparks(spawnPos, XMFLOAT3(0, 1, 0), 50);
        else if (v == "smoke") EngineVFX::ParticleSystem::Get().SpawnSmoke(spawnPos);
        else if (v == "magic") EngineVFX::ParticleSystem::Get().SpawnMagicEnergy(spawnPos);
    });

    // 11. lua
    RegisterCommand("lua", "Evaluate a Lua expression", "lua <code>", [this](const std::vector<std::string>& args) {
        if (args.empty()) return;
        std::string code;
        for (const auto& a : args) code += a + " ";
        std::string err;
        if (EngineScript::ScriptSystem::Get().GetScriptCount() > 0) {
            EngineScript::ScriptSystem::Get().ExecString(1, code, err);
            if (!err.empty()) LogError("[Lua Error] " + err);
            else LogSuccess("[Lua OK]");
        } else {
            LogWarning("No active Lua script context to execute in.");
        }
    });

    // 12. exit
    RegisterCommand("exit", "Close the engine application", "exit", [](const std::vector<std::string>&) {
#if defined(_WIN32)
        PostQuitMessage(0);
#endif
    });
    m_commands["quit"] = m_commands["exit"];
}

#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
static int TextEditCallback(ImGuiInputTextCallbackData* data) {
    DebugConsole* console = static_cast<DebugConsole*>(data->UserData);
    if (!console) return 0;

    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        const auto& history = console->GetHistory();
        int& historyPos = console->GetHistoryPos();
        const int prevHistoryPos = historyPos;
        if (data->EventKey == ImGuiKey_UpArrow) {
            if (historyPos == -1) {
                historyPos = static_cast<int>(history.size()) - 1;
            } else if (historyPos > 0) {
                historyPos--;
            }
        } else if (data->EventKey == ImGuiKey_DownArrow) {
            if (historyPos != -1) {
                if (++historyPos >= static_cast<int>(history.size())) {
                    historyPos = -1;
                }
            }
        }

        if (prevHistoryPos != historyPos) {
            const char* historyStr = (historyPos >= 0 && historyPos < static_cast<int>(history.size())) ? history[historyPos].c_str() : "";
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, historyStr);
        }
    }
    return 0;
}
#endif

void DebugConsole::DrawUI() {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    // Optional FPS HUD Overlay
    if (m_showFpsOverlay) {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.45f);
        if (ImGui::Begin("##FPSOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
            float fps = Profiler::Get().GetCurrentFPS();
            float ft = Profiler::Get().GetCurrentFrameTimeMs();
            ImGui::TextColored(fps >= 59.0f ? ImVec4(0.2f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.4f, 0.2f, 1.0f),
                               "%.1f FPS (%.2f ms)", fps, ft);
        }
        ImGui::End();
    }

    if (!m_isOpen) return;

    ImGui::SetNextWindowSize(ImVec2(750, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(" Developer Console (~)", &m_isOpen)) {
        ImGui::End();
        return;
    }

    // Top action bar
    if (ImGui::Button("Clear")) { Clear(); }
    ImGui::SameLine();
    bool copy = ImGui::Button("Copy Log");
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_scrollToBottom);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "| Esc or ~ to close");

    ImGui::Separator();

    // Log scrolling area
    const float footerHeightToReserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footerHeightToReserve), false, ImGuiWindowFlags_HorizontalScrollbar);

    std::string fullLog;
    for (const auto& msg : m_messages) {
        if (copy) fullLog += msg.text + "\n";

        ImVec4 col = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        switch (msg.level) {
            case LogLevel::Warning: col = ImVec4(1.0f, 0.85f, 0.2f, 1.0f); break;
            case LogLevel::Error:   col = ImVec4(1.0f, 0.35f, 0.35f, 1.0f); break;
            case LogLevel::Success: col = ImVec4(0.3f, 1.0f, 0.4f, 1.0f); break;
            case LogLevel::Command: col = ImVec4(0.4f, 0.8f, 1.0f, 1.0f); break;
            default: break;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(msg.text.c_str());
        ImGui::PopStyleColor();
    }

    if (copy) ImGui::SetClipboardText(fullLog.c_str());

    if (m_scrollToBottom || (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())) {
        ImGui::SetScrollHereY(1.0f);
    }
    m_scrollToBottom = false;
    ImGui::EndChild();

    ImGui::Separator();

    // Command input line
    bool reclaimFocus = m_reclaimFocus;
    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory;
    if (ImGui::InputText("##ConsoleInput", m_inputBuf, sizeof(m_inputBuf), inputFlags, &TextEditCallback, this)) {
        std::string inputStr = m_inputBuf;
        if (!inputStr.empty()) {
            Execute(inputStr);
            m_inputBuf[0] = '\0';
        }
        reclaimFocus = true;
    }

    ImGui::SetItemDefaultFocus();
    if (reclaimFocus) {
        ImGui::SetKeyboardFocusHere(-1);
        m_reclaimFocus = false;
    }

    ImGui::End();
#endif
}

} // namespace EngineDebug
