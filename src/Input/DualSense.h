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

enum class DualSenseConnection {
    Disconnected,
    USB,
    Bluetooth,
    XInput
};

enum DualSenseButton : uint32_t {
    None        = 0,
    Cross       = 1 << 0,  // A / Cross
    Circle      = 1 << 1,  // B / Circle
    Square      = 1 << 2,  // X / Square
    Triangle    = 1 << 3,  // Y / Triangle
    DPadUp      = 1 << 4,
    DPadRight   = 1 << 5,
    DPadDown    = 1 << 6,
    DPadLeft    = 1 << 7,
    L1          = 1 << 8,
    R1          = 1 << 9,
    L2Button    = 1 << 10,
    R2Button    = 1 << 11,
    L3          = 1 << 12, // Left stick click
    R3          = 1 << 13, // Right stick click
    Create      = 1 << 14, // Share / Create
    Options     = 1 << 15, // Start / Options
    PS          = 1 << 16, // PS Home
    Touchpad    = 1 << 17, // Touchpad click
    MicMute     = 1 << 18  // Mute button
};

inline DualSenseButton operator|(DualSenseButton a, DualSenseButton b) {
    return static_cast<DualSenseButton>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline DualSenseButton operator&(DualSenseButton a, DualSenseButton b) {
    return static_cast<DualSenseButton>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

enum class DualSenseTriggerEffect {
    None,
    ContinuousResistance,
    SectionResistance,
    GallopingWeapon,
    Bow,
    RigidStop,
    Vibration,
    Custom
};

struct DualSenseTriggerConfig {
    DualSenseTriggerEffect effect = DualSenseTriggerEffect::None;
    uint8_t mode = 0;
    uint8_t startPosition = 0;  // 0 - 255 (or 0-9)
    uint8_t endPosition = 255;  // 0 - 255
    uint8_t force = 0;          // 0 - 255
    uint8_t frequency = 0;      // 0 - 255 (for galloping/vibration)
    uint8_t extraForce = 0;     // secondary force / snap force
    uint8_t rawParams[10] = {}; // 10 raw parameter bytes

    bool operator==(const DualSenseTriggerConfig& other) const {
        if (effect != other.effect || mode != other.mode ||
            startPosition != other.startPosition || endPosition != other.endPosition ||
            force != other.force || frequency != other.frequency || extraForce != other.extraForce) {
            return false;
        }
        for (int i = 0; i < 10; ++i) {
            if (rawParams[i] != other.rawParams[i]) return false;
        }
        return true;
    }
    bool operator!=(const DualSenseTriggerConfig& other) const {
        return !(*this == other);
    }
};

struct DualSenseTouchPoint {
    bool active = false;
    uint8_t id = 0;
    float x = 0.0f; // 0.0f to 1.0f (0 to 1920 normalized)
    float y = 0.0f; // 0.0f to 1.0f (0 to 1080 normalized)
};

struct DualSenseMotionState {
    // Gyroscope in degrees per second (Pitch, Yaw, Roll)
    float gyroPitch = 0.0f;
    float gyroYaw = 0.0f;
    float gyroRoll = 0.0f;

    // Accelerometer in Gs (1.0 = ~9.81 m/s^2)
    float accelX = 0.0f;
    float accelY = 0.0f;
    float accelZ = 0.0f;

    // Timestamp from controller IMU in microseconds
    uint32_t sensorTimestamp = 0;
};

struct DualSenseBattery {
    uint8_t level = 100;        // 0 to 100%
    bool isCharging = false;
    bool isFull = false;
};

enum class DualSensePlayerLed : uint8_t {
    None    = 0x00,
    Player1 = 0x04, // Center LED
    Player2 = 0x0A, // 2nd and 4th LEDs
    Player3 = 0x15, // 1st, 3rd, 5th LEDs
    Player4 = 0x1B, // 1st, 2nd, 4th, 5th LEDs
    All     = 0x1F  // All 5 LEDs
};

enum class DualSenseMuteLed : uint8_t {
    Off   = 0x00,
    On    = 0x01,
    Pulse = 0x02
};

enum class DualSenseProfilePreset {
    Default,
    Shooter,
    Racing,
    Bow,
    Shotgun,
    OffRoad,
    Custom
};

struct DualSenseProfile {
    std::string name = "Default";
    DualSenseTriggerConfig leftTrigger;
    DualSenseTriggerConfig rightTrigger;
    int lightbarMode = 0; // 0=Static, 1=Rainbow, 2=Battery, 3=AudioReactive, 4=Breath, 5=WeatherSync
    float lightbarColor[3] = { 0.0f, 0.47f, 1.0f }; // PlayStation Cyan/Blue
    DualSensePlayerLed playerLed = DualSensePlayerLed::Player1;
    DualSenseMuteLed muteLed = DualSenseMuteLed::Off;
    bool touchpadAsMouse = false;
    float touchpadSensitivity = 1.2f;
    bool gyroAiming = false;
    float gyroYawSens = 1.0f;
    float gyroPitchSens = 1.0f;
    bool audioToHaptics = true;
    float audioToHapticsGain = 1.0f;
    float stickDeadzone = 0.08f;
    float triggerDeadzone = 0.05f;
};

struct DualSenseState {
    DualSenseConnection connection = DualSenseConnection::Disconnected;
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
    DualSenseMotionState motion;

    // Multi-touch Touchpad (2 points)
    DualSenseTouchPoint touch[2];

    // Battery
    DualSenseBattery battery;
};

class DualSenseController {
public:
    static DualSenseController& Get();

    DualSenseController();
    ~DualSenseController();

    // Lifecycle
    bool Initialize();
    void Shutdown();
    void Update(float dt);

    // Connection & Device Info
    bool IsConnected() const { return m_currentState.connection != DualSenseConnection::Disconnected; }
    DualSenseConnection GetConnectionType() const { return m_currentState.connection; }
    DualSenseConnection GetPhysicalConnectionType() const { return m_physicalConnection; }
    const std::string& GetDevicePath() const { return m_devicePath; }
    const std::wstring& GetProductString() const { return m_productString; }
    bool IsEdgeController() const { return m_isEdge; }
    bool IsDualShock4() const { return m_isDualShock4; }

    // Input Queries
    const DualSenseState& GetState() const { return m_currentState; }
    bool IsButtonDown(DualSenseButton button) const;
    bool IsButtonPressed(DualSenseButton button) const;
    bool IsButtonReleased(DualSenseButton button) const;

    float GetLeftStickX() const { return m_currentState.leftStickX; }
    float GetLeftStickY() const { return m_currentState.leftStickY; }
    float GetRightStickX() const { return m_currentState.rightStickX; }
    float GetRightStickY() const { return m_currentState.rightStickY; }

    float GetLeftTrigger() const { return m_currentState.leftTrigger; }
    float GetRightTrigger() const { return m_currentState.rightTrigger; }

    const DualSenseMotionState& GetMotion() const { return m_currentState.motion; }
    const DualSenseTouchPoint& GetTouch(int index) const { return m_currentState.touch[index == 1 ? 1 : 0]; }
    const DualSenseBattery& GetBattery() const { return m_currentState.battery; }

    // Output & Haptic Controls
    void SetRumble(float leftMotor, float rightMotor); // 0.0f - 1.0f
    void PlayRumble(float leftMotor, float rightMotor, float durationSeconds);
    void SetLightbar(uint8_t r, uint8_t g, uint8_t b);
    void SetLightbarColor(float r, float g, float b);
    void GetLightbar(uint8_t& r, uint8_t& g, uint8_t& b) const { r = m_lightbarR; g = m_lightbarG; b = m_lightbarB; }
    void SetPlayerLed(DualSensePlayerLed led);
    DualSensePlayerLed GetPlayerLed() const { return m_playerLed; }
    void SetMuteLed(DualSenseMuteLed led);
    DualSenseMuteLed GetMuteLed() const { return m_muteLed; }

    // Audio Output Configuration & Hardware Speaker Unmute
    void SetSpeakerVolume(uint8_t volume); // 0 - 100 (0x00 - 0x64 in Sony spec)
    uint8_t GetSpeakerVolume() const { return m_speakerVolume; }
    void SetHeadphoneVolume(uint8_t volume); // 0 - 127 (0x00 - 0x7F)
    uint8_t GetHeadphoneVolume() const { return m_headphoneVolume; }
    void SetAudioRouting(uint8_t routingFlags); // e.g. 0x30 for speaker, 0x20 for both, 0x00 for headphones
    uint8_t GetAudioRouting() const { return m_audioRouting; }

    // Window Lifecycle: Activation on window show, deactivation on window close/minimize
    void Activate();
    void Deactivate() { TurnOffAll(); }
    void Pause();
    void Resume();
    void TurnOffAll();
    bool IsActive() const { return m_isActive; }
    void SetKeepColorInBackground(bool keep) { m_keepColorInBackground = keep; }
    bool GetKeepColorInBackground() const { return m_keepColorInBackground; }

    // Adaptive Trigger Presets & Configuration
    void SetLeftTriggerEffect(const DualSenseTriggerConfig& config);
    void SetRightTriggerEffect(const DualSenseTriggerConfig& config);
    const DualSenseTriggerConfig& GetLeftTriggerConfig() const { return m_leftTriggerConfig; }
    const DualSenseTriggerConfig& GetRightTriggerConfig() const { return m_rightTriggerConfig; }

    void SetLeftTriggerResistance(uint8_t startPos, uint8_t force);
    void SetRightTriggerResistance(uint8_t startPos, uint8_t force);
    void SetRightTriggerWeapon(uint8_t startPos, uint8_t endPos, uint8_t force, uint8_t frequency);
    void SetRightTriggerBow(uint8_t startPos, uint8_t endPos, uint8_t force, uint8_t snapForce);
    void SetLeftTriggerVibration(uint8_t startPos, uint8_t amplitude, uint8_t frequency);
    void SetLeftTriggerVibration(uint8_t startPos, uint8_t endPos, uint8_t amplitude, uint8_t frequency);
    void SetRightTriggerVibration(uint8_t startPos, uint8_t amplitude, uint8_t frequency);
    void SetRightTriggerVibration(uint8_t startPos, uint8_t endPos, uint8_t amplitude, uint8_t frequency);
    void SetTriggersVibration(uint8_t startPos, uint8_t amplitude, uint8_t frequency);
    void SetTriggersVibration(uint8_t startPos, uint8_t endPos, uint8_t amplitude, uint8_t frequency);
    void PlayTriggerVibration(bool left, bool right, uint8_t amplitude, uint8_t frequency, float durationSeconds);
    void StopTriggerVibration(bool left = true, bool right = true);
    bool IsTriggerVibrating(bool right) const;
    void ResetTriggers();

    // Tuning & Calibration
    void SetDeadzone(float stickDeadzone, float triggerDeadzone = 0.05f);
    float GetStickDeadzone() const { return m_stickDeadzone; }
    float GetTriggerDeadzone() const { return m_triggerDeadzone; }

    void SetGyroAimingEnabled(bool enabled) { m_gyroAimingEnabled = enabled; }
    bool IsGyroAimingEnabled() const { return m_gyroAimingEnabled; }
    void SetGyroSensitivity(float yawSens, float pitchSens) { m_gyroYawSensitivity = yawSens; m_gyroPitchSensitivity = pitchSens; }
    float GetGyroYawSensitivity() const { return m_gyroYawSensitivity; }
    float GetGyroPitchSensitivity() const { return m_gyroPitchSensitivity; }
    void CalibrateGyro();

    // Profile System
    DualSenseProfile GetBuiltInProfile(DualSenseProfilePreset preset);
    void ApplyProfile(const DualSenseProfile& profile);
    const DualSenseProfile& GetCurrentProfile() const { return m_activeProfile; }
    DualSenseProfile& GetCurrentProfile() { return m_activeProfile; }
    bool SaveProfile(const std::string& name);
    bool LoadProfile(const std::string& name);
    std::vector<std::string> GetAvailableProfiles() const;

    // Touchpad as Mouse Trackpad
    void SetTouchpadAsMouse(bool enable) { m_touchpadAsMouse = enable; }
    bool IsTouchpadAsMouse() const { return m_touchpadAsMouse; }
    void SetTouchpadSensitivity(float sens) { m_touchpadSensitivity = sens; }
    float GetTouchpadSensitivity() const { return m_touchpadSensitivity; }

    // Audio to Haptics Engine
    void SetAudioToHaptics(bool enable) { m_audioToHaptics = enable; }
    bool IsAudioToHaptics() const { return m_audioToHaptics; }
    void SetAudioToHapticsGain(float gain) { m_audioToHapticsGain = gain; }
    float GetAudioToHapticsGain() const { return m_audioToHapticsGain; }

    // Bluetooth Audio Sync & Dual-Band Haptics
    // Dual-band: Bass energy -> left heavy motor, Treble energy -> right light motor
    void SetAudioToHapticsDualBand(bool enable) { m_audioToHapticsDualBand = enable; }
    bool IsAudioToHapticsDualBand() const { return m_audioToHapticsDualBand; }
    void SetBassHapticsGain(float gain) { m_bassHapticsGain = (gain < 0.0f) ? 0.0f : (gain > 4.0f ? 4.0f : gain); }
    float GetBassHapticsGain() const { return m_bassHapticsGain; }
    void SetTrebleHapticsGain(float gain) { m_trebleHapticsGain = (gain < 0.0f) ? 0.0f : (gain > 4.0f ? 4.0f : gain); }
    float GetTrebleHapticsGain() const { return m_trebleHapticsGain; }
    // Delay compensation so haptic pulse lines up with Bluetooth audio latency (0..150ms)
    void SetBluetoothAudioSyncDelay(float seconds) { m_btAudioSyncDelay = (seconds < 0.0f) ? 0.0f : (seconds > 0.15f ? 0.15f : seconds); }
    float GetBluetoothAudioSyncDelay() const { return m_btAudioSyncDelay; }

    // Lightbar Modes & Dynamic FX
    void SetLightbarMode(int mode) { m_lightbarMode = mode; }
    int GetLightbarMode() const { return m_lightbarMode; }

private:
    void DeviceWorkerThread();
    bool EnumerateAndOpenDevice();
    void CloseDevice();
    void ParseInputReport(const uint8_t* data, size_t size);
    void SendOutputReport();
    void ApplyDeadzone(float rawX, float rawY, float deadzone, float& outX, float& outY);

    static uint32_t CalculateCRC32(const uint8_t* data, size_t size);

private:
    HANDLE m_deviceHandle = INVALID_HANDLE_VALUE;
    DualSenseConnection m_connectionType = DualSenseConnection::Disconnected;
    DualSenseConnection m_physicalConnection = DualSenseConnection::Disconnected;
    std::string m_devicePath;
    std::wstring m_productString;
    bool m_isEdge = false;
    bool m_isDualShock4 = false;
    USHORT m_inputReportLength = 64;
    USHORT m_outputReportLength = 64;

    // Asynchronous I/O Thread
    std::atomic<bool> m_running{ false };
    std::thread m_workerThread;
    std::mutex m_stateMutex;
    std::mutex m_outputMutex;

    // Dual-buffered state
    DualSenseState m_rawState;
    DualSenseState m_currentState;
    DualSenseState m_previousState;

    // Output State
    uint8_t m_leftRumble = 0;
    uint8_t m_rightRumble = 0;
    float m_rumbleTimer = 0.0f;
    uint8_t m_lightbarR = 0;
    uint8_t m_lightbarG = 120;
    uint8_t m_lightbarB = 255;
    DualSensePlayerLed m_playerLed = DualSensePlayerLed::Player1;
    DualSenseMuteLed m_muteLed = DualSenseMuteLed::Off;

    // Hardware Audio Controls
    uint8_t m_speakerVolume = 0x7F;   // 100% max volume in Sony spec [0x00..0x7F]
    uint8_t m_headphoneVolume = 0x7F; // 100% [0x00..0x7F]
    uint8_t m_audioRouting = 0x30;    // Route audio to internal speaker (unmute!)

    DualSenseTriggerConfig m_leftTriggerConfig;
    DualSenseTriggerConfig m_rightTriggerConfig;
    bool m_outputDirty = true;
    float m_outputSendTimer = 0.0f;

    // Window Activation State & Cached Profile
    bool m_isActive = false;
    bool m_keepColorInBackground = false;
    DualSenseTriggerConfig m_savedLeftTrigger;
    DualSenseTriggerConfig m_savedRightTrigger;
    uint8_t m_savedLightR = 0;
    uint8_t m_savedLightG = 120;
    uint8_t m_savedLightB = 255;
    DualSensePlayerLed m_savedPlayerLed = DualSensePlayerLed::Player1;

    // Dynamic Trigger Vibration Pulses
    float m_leftTriggerPulseTimer = 0.0f;
    float m_rightTriggerPulseTimer = 0.0f;
    DualSenseTriggerConfig m_prePulseLeftTrigger;
    DualSenseTriggerConfig m_prePulseRightTrigger;

    // Tuning & Calibration
    float m_stickDeadzone = 0.08f;
    float m_triggerDeadzone = 0.05f;
    bool m_gyroAimingEnabled = false;
    float m_gyroYawSensitivity = 1.0f;
    float m_gyroPitchSensitivity = 1.0f;

    float m_gyroBiasPitch = 0.0f;
    float m_gyroBiasYaw = 0.0f;
    float m_gyroBiasRoll = 0.0f;
    int m_calibrationSamples = 0;
    bool m_calibrating = false;

    // Active Profile
    DualSenseProfile m_activeProfile;

    // Touchpad to Mouse state
    bool m_touchpadAsMouse = false;
    float m_touchpadSensitivity = 1.2f;
    float m_prevTouchX = 0.0f;
    float m_prevTouchY = 0.0f;
    bool m_prevTouchActive = false;
    bool m_prevTouchpadClick = false;

    // Audio to Haptics & Lightbar animations
    bool m_audioToHaptics = true;
    float m_audioToHapticsGain = 1.0f;
    bool m_audioToHapticsDualBand = true;   // Dual-band: Bass->left motor, Treble->right motor
    float m_bassHapticsGain = 1.2f;          // Extra gain for bass->left heavy motor
    float m_trebleHapticsGain = 0.8f;        // Gain for treble->right light motor
    float m_btAudioSyncDelay = 0.0f;         // Bluetooth audio sync delay compensation (seconds)
    float m_btSyncDelayAccum = 0.0f;         // Accumulator for BT haptic delay
    int m_lightbarMode = 0; // 0=Static, 1=Rainbow, 2=Battery, 3=AudioReactive, 4=Breath, 5=WeatherSync
    float m_animTimer = 0.0f;
};

#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
int GetPS4PadHandle();
#endif

} // namespace EngineInput
