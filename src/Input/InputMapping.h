#pragma once

#include "InputTypes.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

class Window;

namespace EngineInput {

class DualSenseController;
class DualShockController;

class InputMapping {
public:
    static InputMapping& Get();

    bool Initialize();
    void Shutdown();

    // Main Update (polls Window keys/mouse and DualSense / DualShock controller)
    void Update(float dt, Window* window, DualSenseController* controller = nullptr, DualShockController* ds4Controller = nullptr);

    // Queries
    bool IsActionDown(const std::string& actionName) const;
    bool IsActionPressed(const std::string& actionName) const;
    bool IsActionReleased(const std::string& actionName) const;
    float GetActionAxis(const std::string& actionName) const;
    DirectX::XMFLOAT2 GetActionAxis2D(const std::string& actionName) const;
    // Raw counts sampled once by Update; reading them never consumes input.
    DirectX::XMFLOAT2 GetFrameMouseDelta() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_frameMouseDelta;
    }

    // Contexts
    void SetActiveContext(const std::string& contextName) { m_activeContext = contextName; }
    const std::string& GetActiveContext() const { return m_activeContext; }

    // Rebinding
    void BindActionKey(const std::string& actionName, int vKey);
    void BindActionGamepad(const std::string& actionName, uint32_t gamepadButton);
    void ResetToDefaults();

    // Settings
    float GetMouseSensitivity() const { return m_mouseSensitivity; }
    void SetMouseSensitivity(float sens) { m_mouseSensitivity = sens; }
    float GetGamepadSensitivity() const { return m_gamepadSensitivity; }
    void SetGamepadSensitivity(float sens) { m_gamepadSensitivity = sens; }
    float GetStickDeadzone() const { return m_stickDeadzone; }
    void SetStickDeadzone(float deadzone) { m_stickDeadzone = deadzone; }
    bool IsInvertY() const { return m_invertY; }
    void SetInvertY(bool invert) { m_invertY = invert; }

    // Serialization
    bool SaveToFile(const std::string& filepath = "");
    bool LoadFromFile(const std::string& filepath = "");

    // UI
    void DrawUI(DualSenseController* controller = nullptr);
    void ToggleUI() { m_uiOpen = !m_uiOpen; }
    void SetUIOpen(bool open) { m_uiOpen = open; }
    bool IsUIOpen() const { return m_uiOpen; }

private:
    InputMapping() = default;
    ~InputMapping() = default;
    InputMapping(const InputMapping&) = delete;
    InputMapping& operator=(const InputMapping&) = delete;

    std::unordered_map<std::string, ActionBinding> m_actions;
    std::unordered_map<std::string, ActionState> m_states;
    std::string m_activeContext = "Gameplay";
    mutable std::recursive_mutex m_mutex;

    float m_mouseSensitivity = 1.0f;
    DirectX::XMFLOAT2 m_frameMouseDelta { 0.0f, 0.0f };
    float m_gamepadSensitivity = 1.5f;
    float m_stickDeadzone = 0.15f;
    bool m_invertY = false;

    bool m_initialized = false;
    bool m_uiOpen = false;

    // Rebinding state
    std::string m_rebindingAction = "";
    bool m_rebindingGamepad = false;

    void SetupDefaultBindings();
};

} // namespace EngineInput
