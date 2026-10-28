#pragma once

#if defined(_WIN32)
#include <windows.h>
#else
#include "Platform/PlatformTypes.h"
#endif
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>

namespace EngineInput {

enum class DualShockConnection {
    Disconnected,
    USB,
    Bluetooth,
    PS4Native
};

enum DualShockButton : uint32_t {
    DS_None        = 0,
    DS_Cross       = 1 << 0,  // Cross / A
    DS_Circle      = 1 << 1,  // Circle / B
    DS_Square      = 1 << 2,  // Square / X
    DS_Triangle    = 1 << 3,  // Triangle / Y
    DS_DPadUp      = 1 << 4,
    DS_DPadRight   = 1 << 5,
    DS_DPadDown    = 1 << 6,
    DS_DPadLeft    = 1 << 7,
    DS_L1          = 1 << 8,
    DS_R1          = 1 << 9,
    DS_L2Button    = 1 << 10,
    DS_R2Button    = 1 << 11,
    DS_L3          = 1 << 12, // Left stick click
    DS_R3          = 1 << 13, // Right stick click
    DS_Share       = 1 << 14, // Share
    DS_Options     = 1 << 15, // Options
    DS_PS          = 1 << 16, // PS Button
    DS_Touchpad    = 1 << 17  // Touchpad click
};

inline DualShockButton operator|(DualShockButton a, DualShockButton b) {
    return static_cast<DualShockButton>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline DualShockButton operator&(DualShockButton a, DualShockButton b) {
    return static_cast<DualShockButton>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

struct DualShockTouchPoint {
    bool active = false;
    uint8_t id = 0;
    float x = 0.0f; // 0.0f to 1.0f (0 to 1920 normalized)
    float y = 0.0f; // 0.0f to 1.0f (0 to 942 normalized)
};

struct DualShockMotionState {
    // Gyroscope in degrees per second (Pitch, Yaw, Roll)
    float gyroPitch = 0.0f;
    float gyroYaw = 0.0f;
    float gyroRoll = 0.0f;

    // Accelerometer in Gs
    float accelX = 0.0f;
    float accelY = 0.0f;
    float accelZ = 0.0f;

    uint32_t sensorTimestamp = 0;
};

struct DualShockBattery {
    uint8_t level = 100; // 0 to 100%
    bool isCharging = false;
    bool isFull = false;
};

struct DualShockState {
    DualShockConnection connection = DualShockConnection::Disconnected;
    uint32_t buttons = 0;

    // Analog Sticks: [-1.0f, +1.0f], (0, 0 is center)
    float leftStickX = 0.0f;
    float leftStickY = 0.0f;
    float rightStickX = 0.0f;
    float rightStickY = 0.0f;

    // Analog Triggers: [0.0f, 1.0f]
    float leftTrigger = 0.0f;
    float rightTrigger = 0.0f;

    // Motion & IMU
    DualShockMotionState motion;

    // Multi-touch Touchpad (2 points)
    DualShockTouchPoint touch[2];

    // Battery
    DualShockBattery battery;
};

class DualShockController {
public:
    static DualShockController& Get();

    DualShockController();
    ~DualShockController();

    // Lifecycle
    bool Initialize();
    void Shutdown();
    void Update(float dt);

    // Connection & Device Info
    bool IsConnected() const { return m_currentState.connection != DualShockConnection::Disconnected; }
    DualShockConnection GetConnectionType() const { return m_currentState.connection; }
    const std::string& GetDevicePath() const { return m_devicePath; }
    const std::wstring& GetProductString() const { return m_productString; }

    // Input Queries
    const DualShockState& GetState() const { return m_currentState; }
    bool IsButtonDown(DualShockButton button) const;
    bool IsButtonPressed(DualShockButton button) const;
    bool IsButtonReleased(DualShockButton button) const;

    float GetLeftStickX() const { return m_currentState.leftStickX; }
    float GetLeftStickY() const { return m_currentState.leftStickY; }
    float GetRightStickX() const { return m_currentState.rightStickX; }
    float GetRightStickY() const { return m_currentState.rightStickY; }

    float GetLeftTrigger() const { return m_currentState.leftTrigger; }
    float GetRightTrigger() const { return m_currentState.rightTrigger; }

    const DualShockMotionState& GetMotion() const { return m_currentState.motion; }
    const DualShockTouchPoint& GetTouch(int index) const { return m_currentState.touch[index == 1 ? 1 : 0]; }
    const DualShockBattery& GetBattery() const { return m_currentState.battery; }

    // Output & Lightbar Controls
    void SetRumble(float heavyMotor, float smallMotor); // 0.0f - 1.0f
    void PlayRumble(float heavyMotor, float smallMotor, float durationSeconds);
    void SetLightbar(uint8_t r, uint8_t g, uint8_t b);
    void SetLightbarColor(float r, float g, float b);
    void GetLightbar(uint8_t& r, uint8_t& g, uint8_t& b) const { r = m_lightbarR; g = m_lightbarG; b = m_lightbarB; }
    void TurnOffAll();

    // Deadzone & Sensitivity Tuning
    void SetDeadzone(float stickDeadzone, float triggerDeadzone = 0.05f);
    float GetStickDeadzone() const { return m_stickDeadzone; }
    float GetTriggerDeadzone() const { return m_triggerDeadzone; }

    // Touchpad as Mouse
    void SetTouchpadAsMouse(bool enable) { m_touchpadAsMouse = enable; }
    bool IsTouchpadAsMouse() const { return m_touchpadAsMouse; }
    void SetTouchpadSensitivity(float sens) { m_touchpadSensitivity = sens; }
    float GetTouchpadSensitivity() const { return m_touchpadSensitivity; }

    // Gyro Aiming
    void SetGyroAimingEnabled(bool enabled) { m_gyroAimingEnabled = enabled; }
    bool IsGyroAimingEnabled() const { return m_gyroAimingEnabled; }
    void SetGyroSensitivity(float yawSens, float pitchSens) { m_gyroYawSensitivity = yawSens; m_gyroPitchSensitivity = pitchSens; }
    float GetGyroYawSensitivity() const { return m_gyroYawSensitivity; }
    float GetGyroPitchSensitivity() const { return m_gyroPitchSensitivity; }

private:
#if defined(_WIN32)
    void DeviceWorkerThread();
    bool EnumerateAndOpenDevice();
    void CloseDevice();
    void ParseInputReport(const uint8_t* data, size_t size);
    void SendOutputReport();
#endif

    void ApplyDeadzone(float rawX, float rawY, float deadzone, float& outX, float& outY);
    static uint32_t CalculateCRC32(const uint8_t* data, size_t size);

private:
    DualShockConnection m_connectionType = DualShockConnection::Disconnected;
    DualShockConnection m_physicalConnection = DualShockConnection::Disconnected;
    std::string m_devicePath;
    std::wstring m_productString;

#if defined(_WIN32)
    HANDLE m_deviceHandle = INVALID_HANDLE_VALUE;
    USHORT m_inputReportLength = 64;
    USHORT m_outputReportLength = 64;

    std::atomic<bool> m_running{ false };
    std::thread m_workerThread;
#endif

    std::mutex m_stateMutex;
    std::mutex m_outputMutex;

    DualShockState m_rawState;
    DualShockState m_currentState;
    DualShockState m_previousState;

    uint8_t m_heavyRumble = 0;
    uint8_t m_smallRumble = 0;
    float m_rumbleTimer = 0.0f;
    uint8_t m_lightbarR = 0;
    uint8_t m_lightbarG = 64;
    uint8_t m_lightbarB = 255;
    bool m_outputDirty = true;

    float m_stickDeadzone = 0.08f;
    float m_triggerDeadzone = 0.05f;

    bool m_touchpadAsMouse = false;
    float m_touchpadSensitivity = 1.2f;
    float m_prevTouchX = 0.0f;
    float m_prevTouchY = 0.0f;
    bool m_prevTouchActive = false;
    bool m_prevTouchpadClick = false;

    bool m_gyroAimingEnabled = false;
    float m_gyroYawSensitivity = 1.0f;
    float m_gyroPitchSensitivity = 1.0f;
};

} // namespace EngineInput
