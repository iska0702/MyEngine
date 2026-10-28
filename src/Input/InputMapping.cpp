#include "InputMapping.h"
#include "DualShock.h"
#include "Window.h"
#include "Platform/PlatformUtils.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cmath>

#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
#include <imgui.h>
#endif

namespace EngineInput {

InputMapping& InputMapping::Get() {
    static InputMapping instance;
    return instance;
}

bool InputMapping::Initialize() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_initialized) return true;
    m_initialized = true;

    std::cout << "[InputMapping] Initializing Action-Based Input System..." << std::endl;

    SetupDefaultBindings();
    LoadFromFile();

    std::cout << "[InputMapping] Initialized with " << m_actions.size() << " action mappings." << std::endl;
    return true;
}

void InputMapping::Shutdown() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_actions.clear();
    m_states.clear();
    m_initialized = false;
}

void InputMapping::SetupDefaultBindings() {
    m_actions.clear();

    // 1. Move2D (WASD + Left Stick)
    ActionBinding move;
    move.name = "Move2D";
    move.type = ActionType::Axis2D;
    move.description = "Move Character Forward/Back/Left/Right";
    // Keyboard: W (+Y), S (-Y), D (+X), A (-X)
    move.keyboardBindings = {
        { 'W', -1, 0, false, 0,  1.0f },
        { 'S', -1, 0, false, 0, -1.0f },
        { 'D', -1, 0, false, 0,  1.0f },
        { 'A', -1, 0, false, 0, -1.0f }
    };
    // Gamepad: Left Stick X & Y
    move.gamepadBindings = {
        { 0, -1, 0, true, 0, 1.0f }, // LeftStickX
        { 0, -1, 0, true, 1, 1.0f }  // LeftStickY
    };
    m_actions[move.name] = move;

    // 2. Look2D (Mouse Delta + Right Stick)
    ActionBinding look;
    look.name = "Look2D";
    look.type = ActionType::Axis2D;
    look.description = "Look Camera Yaw/Pitch";
    look.gamepadBindings = {
        { 0, -1, 0, true, 2, 1.0f }, // RightStickX
        { 0, -1, 0, true, 3, 1.0f }  // RightStickY
    };
    m_actions[look.name] = look;

    // 3. Jump (Space + Cross / A)
    ActionBinding jump;
    jump.name = "Jump";
    jump.type = ActionType::Button;
    jump.description = "Jump Character";
    jump.keyboardBindings = { { VK_SPACE, -1, 0 } };
    jump.gamepadBindings = { { 0, -1, DualSenseButton::Cross } };
    m_actions[jump.name] = jump;

    // 4. Sprint (Shift + L3)
    ActionBinding sprint;
    sprint.name = "Sprint";
    sprint.type = ActionType::Button;
    sprint.description = "Sprint / Boost";
    sprint.keyboardBindings = { { VK_SHIFT, -1, 0 } };
    sprint.gamepadBindings = { { 0, -1, DualSenseButton::L3 } };
    m_actions[sprint.name] = sprint;

    // 5. Crouch (Ctrl + Circle / B)
    ActionBinding crouch;
    crouch.name = "Crouch";
    crouch.type = ActionType::Button;
    crouch.description = "Crouch / Slide";
    crouch.keyboardBindings = { { VK_CONTROL, -1, 0 } };
    crouch.gamepadBindings = { { 0, -1, DualSenseButton::Circle } };
    m_actions[crouch.name] = crouch;

    // 6. Interact (E + Triangle / Y)
    ActionBinding interact;
    interact.name = "Interact";
    interact.type = ActionType::Button;
    interact.description = "Interact / Pick Up";
    interact.keyboardBindings = { { 'E', -1, 0 } };
    interact.gamepadBindings = { { 0, -1, DualSenseButton::Triangle } };
    m_actions[interact.name] = interact;

    // 7. Fire (LMB + R2)
    ActionBinding fire;
    fire.name = "Fire";
    fire.type = ActionType::Button;
    fire.description = "Primary Fire / Attack";
    fire.keyboardBindings = { { 0, 0, 0 } }; // LMB
    fire.gamepadBindings = { { 0, -1, DualSenseButton::R2Button } };
    m_actions[fire.name] = fire;

    // 8. Aim (RMB + L2)
    ActionBinding aim;
    aim.name = "Aim";
    aim.type = ActionType::Button;
    aim.description = "Secondary Aim / Zoom";
    aim.keyboardBindings = { { 0, 1, 0 } }; // RMB
    aim.gamepadBindings = { { 0, -1, DualSenseButton::L2Button } };
    m_actions[aim.name] = aim;

    // 9. Reload (R + Square / X)
    ActionBinding reload;
    reload.name = "Reload";
    reload.type = ActionType::Button;
    reload.description = "Reload Weapon";
    reload.keyboardBindings = { { 'R', -1, 0 } };
    reload.gamepadBindings = { { 0, -1, DualSenseButton::Square } };
    m_actions[reload.name] = reload;

    // 10. ToggleConsole (~ / Tilde)
    ActionBinding console;
    console.name = "ToggleConsole";
    console.type = ActionType::Button;
    console.description = "Open Developer Console";
    console.keyboardBindings = { { VK_OEM_3, -1, 0 } };
    m_actions[console.name] = console;

    // 11. TogglePause (Esc + Options)
    ActionBinding pause;
    pause.name = "TogglePause";
    pause.type = ActionType::Button;
    pause.description = "Pause / Game Menu";
    pause.keyboardBindings = { { VK_ESCAPE, -1, 0 } };
    pause.gamepadBindings = { { 0, -1, DualSenseButton::Options } };
    m_actions[pause.name] = pause;
}

void InputMapping::ResetToDefaults() {
    SetupDefaultBindings();
    SaveToFile();
}

static float ApplyRadialDeadzone(float x, float y, float deadzone, float& outX, float& outY) {
    float mag = std::sqrt(x * x + y * y);
    if (mag < deadzone) {
        outX = 0.0f;
        outY = 0.0f;
        return 0.0f;
    }
    float normMag = (mag - deadzone) / (1.0f - deadzone);
    normMag = (std::min)(1.0f, normMag);
    outX = (x / mag) * normMag;
    outY = (y / mag) * normMag;
    return normMag;
}

void InputMapping::Update(float dt, Window* window, DualSenseController* controller, DualShockController* ds4Controller) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    // Mouse delta from window
    int mouseDx = 0, mouseDy = 0;
    if (window) {
        window->GetMouseDelta(mouseDx, mouseDy);
    }
    m_frameMouseDelta = { static_cast<float>(mouseDx), static_cast<float>(mouseDy) };

    bool isDs5 = (controller && controller->IsConnected());
    bool isDs4 = (!isDs5 && ds4Controller && ds4Controller->IsConnected());
    bool isPadConnected = isDs5 || isDs4;

    // Rebinding capture mode
    if (!m_rebindingAction.empty()) {
#if defined(_WIN32)
        if (!m_rebindingGamepad) {
            // Check all virtual keys
            for (int k = 1; k < 256; ++k) {
                if (k == VK_OEM_3) continue; // Reserve tilde for console
                if (GetAsyncKeyState(k) & 1) {
                    BindActionKey(m_rebindingAction, k);
                    m_rebindingAction = "";
                    break;
                }
            }
        } else if (isPadConnected) {
            if (isDs5) {
                for (uint32_t b = 1; b <= DualSenseButton::MicMute; b <<= 1) {
                    if (controller->IsButtonPressed(static_cast<DualSenseButton>(b))) {
                        BindActionGamepad(m_rebindingAction, b);
                        m_rebindingAction = "";
                        break;
                    }
                }
            } else if (isDs4) {
                for (uint32_t b = 1; b <= DualShockButton::DS_Touchpad; b <<= 1) {
                    if (ds4Controller->IsButtonPressed(static_cast<DualShockButton>(b))) {
                        BindActionGamepad(m_rebindingAction, b);
                        m_rebindingAction = "";
                        break;
                    }
                }
            }
        }
#endif
    }
    m_frameMouseDelta = { static_cast<float>(mouseDx), static_cast<float>(mouseDy) };

    for (auto& pair : m_actions) {
        const auto& name = pair.first;
        const auto& def = pair.second;
        auto& state = m_states[name];

        state.wasDown = state.isDown;
        state.isDown = false;
        state.value1D = 0.0f;
        state.value2D = { 0.0f, 0.0f };

        // -------------------------------------------------------------
        // 1. Digital Button Actions
        // -------------------------------------------------------------
        if (def.type == ActionType::Button) {
            // Check Keyboard / Mouse
            if (window) {
                for (const auto& kb : def.keyboardBindings) {
                    if (kb.vKey > 0 && window->IsKeyDown(kb.vKey)) {
                        state.isDown = true;
                        break;
                    }
#if defined(_WIN32)
                    if (kb.mouseButton == 0 && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                        state.isDown = true;
                        break;
                    } else if (kb.mouseButton == 1 && (GetAsyncKeyState(VK_RBUTTON) & 0x8000)) {
                        state.isDown = true;
                        break;
                    } else if (kb.mouseButton == 2 && (GetAsyncKeyState(VK_MBUTTON) & 0x8000)) {
                        state.isDown = true;
                        break;
                    }
#endif
                }
            }

            // Check Gamepad
            if (isPadConnected && !state.isDown) {
                for (const auto& gb : def.gamepadBindings) {
                    if (gb.gamepadButton > 0) {
                        if (isDs5 && controller->IsButtonDown(static_cast<DualSenseButton>(gb.gamepadButton))) {
                            state.isDown = true;
                            break;
                        } else if (isDs4 && ds4Controller->IsButtonDown(static_cast<DualShockButton>(gb.gamepadButton))) {
                            state.isDown = true;
                            break;
                        }
                    }
                }
            }

            state.isPressed = state.isDown && !state.wasDown;
            state.isReleased = !state.isDown && state.wasDown;
            state.value1D = state.isDown ? 1.0f : 0.0f;
        }
        // -------------------------------------------------------------
        // 2. Axis2D Actions (Move2D, Look2D)
        // -------------------------------------------------------------
        else if (def.type == ActionType::Axis2D) {
            float moveX = 0.0f;
            float moveY = 0.0f;

            if (name == "Move2D") {
                if (window) {
                    if (window->IsKeyDown('W')) moveY += 1.0f;
                    if (window->IsKeyDown('S')) moveY -= 1.0f;
                    if (window->IsKeyDown('D')) moveX += 1.0f;
                    if (window->IsKeyDown('A')) moveX -= 1.0f;
                }

                if (isPadConnected) {
                    float padX = isDs5 ? controller->GetLeftStickX() : ds4Controller->GetLeftStickX();
                    float padY = isDs5 ? controller->GetLeftStickY() : ds4Controller->GetLeftStickY();
                    float dzX = 0.0f, dzY = 0.0f;
                    ApplyRadialDeadzone(padX, padY, m_stickDeadzone, dzX, dzY);
                    moveX += dzX;
                    moveY += dzY;
                }

                float len = std::sqrt(moveX * moveX + moveY * moveY);
                if (len > 1.0f) {
                    moveX /= len;
                    moveY /= len;
                }
            } else if (name == "Look2D") {
                // Mouse delta look
                moveX = static_cast<float>(mouseDx) * m_mouseSensitivity * 0.1f;
                moveY = static_cast<float>(mouseDy) * m_mouseSensitivity * 0.1f * (m_invertY ? -1.0f : 1.0f);

                // Gamepad right stick look
                if (isPadConnected) {
                    float padX = isDs5 ? controller->GetRightStickX() : ds4Controller->GetRightStickX();
                    float padY = isDs5 ? controller->GetRightStickY() : ds4Controller->GetRightStickY();
                    float dzX = 0.0f, dzY = 0.0f;
                    ApplyRadialDeadzone(padX, padY, m_stickDeadzone, dzX, dzY);

                    moveX += dzX * m_gamepadSensitivity * 120.0f * dt;
                    moveY += dzY * m_gamepadSensitivity * 120.0f * dt * (m_invertY ? -1.0f : 1.0f);
                }
            }

            state.value2D = { moveX, moveY };
            state.isDown = (std::abs(moveX) > 0.05f || std::abs(moveY) > 0.05f);
            state.isPressed = state.isDown && !state.wasDown;
            state.isReleased = !state.isDown && state.wasDown;
        }
    }
}

bool InputMapping::IsActionDown(const std::string& actionName) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_states.find(actionName);
    if (it != m_states.end()) return it->second.isDown;
    return false;
}

bool InputMapping::IsActionPressed(const std::string& actionName) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_states.find(actionName);
    if (it != m_states.end()) return it->second.isPressed;
    return false;
}

bool InputMapping::IsActionReleased(const std::string& actionName) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_states.find(actionName);
    if (it != m_states.end()) return it->second.isReleased;
    return false;
}

float InputMapping::GetActionAxis(const std::string& actionName) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_states.find(actionName);
    if (it != m_states.end()) return it->second.value1D;
    return 0.0f;
}

DirectX::XMFLOAT2 InputMapping::GetActionAxis2D(const std::string& actionName) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_states.find(actionName);
    if (it != m_states.end()) return it->second.value2D;
    return { 0.0f, 0.0f };
}

void InputMapping::BindActionKey(const std::string& actionName, int vKey) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_actions.find(actionName);
    if (it != m_actions.end()) {
        it->second.keyboardBindings.clear();
        it->second.keyboardBindings.push_back({ vKey, -1, 0 });
        SaveToFile();
    }
}

void InputMapping::BindActionGamepad(const std::string& actionName, uint32_t gamepadButton) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_actions.find(actionName);
    if (it != m_actions.end()) {
        it->second.gamepadBindings.clear();
        it->second.gamepadBindings.push_back({ 0, -1, gamepadButton });
        SaveToFile();
    }
}

bool InputMapping::SaveToFile(const std::string& filepath) {
    std::string target = filepath.empty() ? EnginePlatform::PlatformUtils::ResolveContentPath("Config/input_bindings.ini") : filepath;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(target).parent_path(), ec);

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::ofstream file(target);
    if (!file.is_open()) return false;

    file << "# MyEngine Input Mapping Configuration\n";
    file << "[Settings]\n";
    file << "mouse_sensitivity = " << m_mouseSensitivity << "\n";
    file << "gamepad_sensitivity = " << m_gamepadSensitivity << "\n";
    file << "stick_deadzone = " << m_stickDeadzone << "\n";
    file << "invert_y = " << (m_invertY ? 1 : 0) << "\n\n";

    file << "[Bindings]\n";
    for (const auto& pair : m_actions) {
        file << pair.first << "_key = ";
        if (!pair.second.keyboardBindings.empty()) {
            file << pair.second.keyboardBindings[0].vKey << "\n";
        } else {
            file << "0\n";
        }
        file << pair.first << "_pad = ";
        if (!pair.second.gamepadBindings.empty()) {
            file << pair.second.gamepadBindings[0].gamepadButton << "\n";
        } else {
            file << "0\n";
        }
    }
    return true;
}

bool InputMapping::LoadFromFile(const std::string& filepath) {
    std::string target = filepath.empty() ? EnginePlatform::PlatformUtils::ResolveContentPath("Config/input_bindings.ini") : filepath;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::ifstream file(target);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        val.erase(0, val.find_first_not_of(" \t"));
        val.erase(val.find_last_not_of(" \t") + 1);

        if (key == "mouse_sensitivity") m_mouseSensitivity = std::stof(val);
        else if (key == "gamepad_sensitivity") m_gamepadSensitivity = std::stof(val);
        else if (key == "stick_deadzone") m_stickDeadzone = std::stof(val);
        else if (key == "invert_y") m_invertY = (val == "1" || val == "true");
    }
    return true;
}

static const char* GetKeyName(int vKey) {
    if (vKey >= 'A' && vKey <= 'Z') {
        static char buf[2] = { 0, 0 };
        buf[0] = static_cast<char>(vKey);
        return buf;
    }
    switch (vKey) {
        case VK_SPACE:   return "Space";
        case VK_SHIFT:   return "Shift";
        case VK_CONTROL: return "Ctrl";
        case VK_ESCAPE:  return "Esc";
        case VK_RETURN:  return "Enter";
        case VK_TAB:     return "Tab";
        case VK_OEM_3:   return "~ (Tilde)";
        default:         return "Key";
    }
}

static const char* GetGamepadButtonName(uint32_t btn) {
    switch (btn) {
        case DualSenseButton::Cross:     return " Cross";
        case DualSenseButton::Circle:    return "◯ Circle";
        case DualSenseButton::Square:    return "▢ Square";
        case DualSenseButton::Triangle:  return "△ Triangle";
        case DualSenseButton::L1:        return "L1";
        case DualSenseButton::R1:        return "R1";
        case DualSenseButton::L2Button:  return "L2 Trigger";
        case DualSenseButton::R2Button:  return "R2 Trigger";
        case DualSenseButton::L3:        return "L3 (Left Stick Click)";
        case DualSenseButton::R3:        return "R3 (Right Stick Click)";
        case DualSenseButton::Options:   return "Options";
        case DualSenseButton::Create:    return "Create / Share";
        case DualSenseButton::DPadUp:    return "D-Pad Up";
        case DualSenseButton::DPadDown:  return "D-Pad Down";
        case DualSenseButton::DPadLeft:  return "D-Pad Left";
        case DualSenseButton::DPadRight: return "D-Pad Right";
        default:                         return "None";
    }
}

void InputMapping::DrawUI(DualSenseController* controller) {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    if (!m_uiOpen) return;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    ImGui::SetNextWindowSize(ImVec2(780, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(" Input Mapping & Controller Studio", &m_uiOpen)) {
        ImGui::End();
        return;
    }

    // Top Controller Status
    bool connected = controller && controller->IsConnected();
    ImGui::Text("Active Input Device:");
    ImGui::SameLine();
    if (connected) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), " DualSense Wireless Controller (Connected)");
        ImGui::SameLine();
        const auto& batt = controller->GetBattery();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[Battery: %d%%]", batt.level);
    } else {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), " Keyboard & Mouse Only (No Gamepad Detected)");
    }

    ImGui::Separator();

    // Split into 2 columns: Left for Actions, Right for Sticks & Triggers live monitor
    ImGui::Columns(2, "InputStudioCols", true);
    ImGui::SetColumnWidth(0, 480);

    // Column 1: Action Bindings Table
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Action Bindings:");

    if (!m_rebindingAction.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "PRESS ANY %s TO BIND TO '%s'...",
                           m_rebindingGamepad ? "GAMEPAD BUTTON" : "KEYBOARD KEY",
                           m_rebindingAction.c_str());
    }

    if (ImGui::BeginTable("BindingsTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Gamepad", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        for (const auto& pair : m_actions) {
            const auto& name = pair.first;
            const auto& def = pair.second;
            const auto& state = m_states[name];

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name.c_str());

            // Keyboard Column
            ImGui::TableNextColumn();
            std::string kbStr = "None";
            if (!def.keyboardBindings.empty()) {
                if (def.keyboardBindings[0].mouseButton == 0) kbStr = "Mouse Left";
                else if (def.keyboardBindings[0].mouseButton == 1) kbStr = "Mouse Right";
                else kbStr = GetKeyName(def.keyboardBindings[0].vKey);
            }
            if (ImGui::SmallButton((kbStr + "##kb_" + name).c_str())) {
                m_rebindingAction = name;
                m_rebindingGamepad = false;
            }

            // Gamepad Column
            ImGui::TableNextColumn();
            std::string padStr = "None";
            if (!def.gamepadBindings.empty()) {
                if (def.gamepadBindings[0].isGamepadAxis) padStr = "Stick/Axis";
                else padStr = GetGamepadButtonName(def.gamepadBindings[0].gamepadButton);
            }
            if (ImGui::SmallButton((padStr + "##pad_" + name).c_str())) {
                m_rebindingAction = name;
                m_rebindingGamepad = true;
            }

            // State Column
            ImGui::TableNextColumn();
            if (state.isDown) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "DOWN");
            } else {
                ImGui::TextDisabled("-");
            }
        }
        ImGui::EndTable();
    }

    if (ImGui::Button("Reset Defaults")) {
        ResetToDefaults();
    }

    ImGui::NextColumn();

    // Column 2: Live Sticks & Triggers visualizer
    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Live Analog Telemetry:");
    ImGui::Separator();

    ImGui::SliderFloat("Mouse Sens", &m_mouseSensitivity, 0.1f, 3.0f, "%.2f");
    ImGui::SliderFloat("Pad Sens", &m_gamepadSensitivity, 0.1f, 3.0f, "%.2f");
    ImGui::SliderFloat("Stick Deadzone", &m_stickDeadzone, 0.05f, 0.4f, "%.2f");
    ImGui::Checkbox("Invert Y Axis", &m_invertY);

    ImGui::Separator();

    if (connected) {
        ImGui::Text("Left Stick: (%.2f, %.2f)", controller->GetLeftStickX(), controller->GetLeftStickY());
        ImGui::Text("Right Stick: (%.2f, %.2f)", controller->GetRightStickX(), controller->GetRightStickY());

        ImGui::Spacing();
        ImGui::Text("Triggers:");
        ImGui::ProgressBar(controller->GetLeftTrigger(), ImVec2(100, 16), "L2");
        ImGui::SameLine();
        ImGui::ProgressBar(controller->GetRightTrigger(), ImVec2(100, 16), "R2");
    } else {
        ImGui::TextDisabled("Connect a DualSense or Gamepad to test live analog sticks & trigger haptics.");
    }

    ImGui::Columns(1);
    ImGui::End();
#endif
}

} // namespace EngineInput
