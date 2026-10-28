#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <DirectXMath.h>
#include "DualSense.h"

namespace EngineInput {

enum class ActionType : uint32_t {
    Button, // Digital true/false (pressed, held, released)
    Axis1D, // Analog float [0.0, 1.0] or [-1.0, 1.0]
    Axis2D  // Analog 2D vector (X, Y)
};

struct KeyBinding {
    int vKey = 0;                     // Windows Virtual Key (e.g. 'W', VK_SPACE, VK_SHIFT)
    int mouseButton = -1;             // -1=None, 0=LMB, 1=RMB, 2=MMB
    uint32_t gamepadButton = 0;       // DualSenseButton bitmask
    bool isGamepadAxis = false;       // Reads from thumbstick or trigger
    int gamepadAxisIndex = 0;         // 0=LeftStickX, 1=LeftStickY, 2=RightStickX, 3=RightStickY, 4=L2, 5=R2
    float scale = 1.0f;               // Axis scale (+1.0 or -1.0)
};

struct ActionState {
    bool isDown = false;
    bool wasDown = false;
    bool isPressed = false;   // Triggered this frame
    bool isReleased = false;  // Released this frame
    float value1D = 0.0f;
    DirectX::XMFLOAT2 value2D = { 0.0f, 0.0f };
};

struct ActionBinding {
    std::string name;
    ActionType type = ActionType::Button;
    std::string description;
    std::vector<KeyBinding> keyboardBindings;
    std::vector<KeyBinding> gamepadBindings;
};

} // namespace EngineInput
