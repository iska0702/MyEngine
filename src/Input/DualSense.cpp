#include "DualSense.h"
#include "DualSenseAudio.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <xinput.h>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <direct.h>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "xinput.lib")

// Universal HID Device Interface Class GUID {4D1E55B2-F16F-11CF-88CB-001111000030}
static const GUID GUID_DEVINTERFACE_HID_CLASS = { 0x4D1E55B2L, 0xF16F, 0x11CF, { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

namespace EngineInput {

// Sony Corporation USB Vendor ID
static constexpr USHORT SONY_VENDOR_ID = 0x054C;
// Standard DualSense (PS5) Product ID
static constexpr USHORT DUALSENSE_PRODUCT_ID = 0x0CE6;
// DualSense Edge Product ID
static constexpr USHORT DUALSENSE_EDGE_PRODUCT_ID = 0x0DF2;
// DualShock 4 Gen 1 (CUH-ZCT1x) Product ID
static constexpr USHORT DUALSHOCK4_GEN1_PRODUCT_ID = 0x05C4;
// DualShock 4 Gen 2 (CUH-ZCT2x) Product ID
static constexpr USHORT DUALSHOCK4_GEN2_PRODUCT_ID = 0x09CC;
// DualShock 4 Wireless USB Adaptor / Dongle Product ID
static constexpr USHORT DUALSHOCK4_DONGLE_PRODUCT_ID = 0x0BA0;

// Standard CRC32 table for Bluetooth output packet validation
static uint32_t s_crc32Table[256];
static bool s_crc32TableInitialized = false;

static void InitializeCRC32Table() {
    if (s_crc32TableInitialized) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        s_crc32Table[i] = crc;
    }
    s_crc32TableInitialized = true;
}

uint32_t DualSenseController::CalculateCRC32(const uint8_t* data, size_t size) {
    InitializeCRC32Table();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; ++i) {
        crc = (crc >> 8) ^ s_crc32Table[(crc ^ data[i]) & 0xFF];
    }
    return ~crc;
}

DualSenseController& DualSenseController::Get() {
    static DualSenseController s_instance;
    return s_instance;
}

DualSenseController::DualSenseController() {
    InitializeCRC32Table();

    // Default Gameplay Adaptive Triggers cached:
    // Left Trigger (L2 - Aim): Continuous resistance for realistic ADS pull
    m_savedLeftTrigger.effect = DualSenseTriggerEffect::ContinuousResistance;
    m_savedLeftTrigger.startPosition = 35;
    m_savedLeftTrigger.force = 140;

    // Right Trigger (R2 - Fire): Tactile Trigger Vibration for realistic recoil feedback
    m_savedRightTrigger.effect = DualSenseTriggerEffect::Vibration;
    m_savedRightTrigger.startPosition = 25;
    m_savedRightTrigger.endPosition = 255;
    m_savedRightTrigger.force = 220;
    m_savedRightTrigger.frequency = 42;

    m_savedLightR = 0;
    m_savedLightG = 120;
    m_savedLightB = 255;
    m_savedPlayerLed = DualSensePlayerLed::Player1;

    // Start active by default with default PlayStation cyan lightbar and Player 1 LED
    m_isActive = true;
    m_leftTriggerConfig = m_savedLeftTrigger;
    m_rightTriggerConfig = m_savedRightTrigger;
    m_lightbarR = 0;
    m_lightbarG = 120;
    m_lightbarB = 255;
    m_playerLed = DualSensePlayerLed::Player1;
    m_muteLed = DualSenseMuteLed::Off;

    m_outputDirty = true;
}

DualSenseController::~DualSenseController() {
    Shutdown();
}

bool DualSenseController::Initialize() {
    if (m_running.load()) return true;

    m_running.store(true);
    m_workerThread = std::thread(&DualSenseController::DeviceWorkerThread, this);
    return true;
}

void DualSenseController::Shutdown() {
    if (!m_running.load()) return;

    m_running.store(false);

    // Turn off all lights, triggers, and rumble before closing handle
    TurnOffAll();

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    CloseDevice();
}

void DualSenseController::Activate() {
    m_isActive = true;

    if (m_savedLeftTrigger.effect == DualSenseTriggerEffect::None) {
        m_savedLeftTrigger.effect = DualSenseTriggerEffect::ContinuousResistance;
        m_savedLeftTrigger.startPosition = 35;
        m_savedLeftTrigger.force = 140;
    }
    if (m_savedRightTrigger.effect == DualSenseTriggerEffect::None || m_savedRightTrigger.effect == DualSenseTriggerEffect::GallopingWeapon) {
        m_savedRightTrigger.effect = DualSenseTriggerEffect::Vibration;
        m_savedRightTrigger.startPosition = 25;
        m_savedRightTrigger.endPosition = 255;
        m_savedRightTrigger.force = 220;
        m_savedRightTrigger.frequency = 42;
    }

    if (m_savedLightR == 0 && m_savedLightG == 0 && m_savedLightB == 0) {
        m_savedLightR = 0;
        m_savedLightG = 120;
        m_savedLightB = 255;
    }
    if (m_savedPlayerLed == DualSensePlayerLed::None) {
        m_savedPlayerLed = DualSensePlayerLed::Player1;
    }

    {
        std::lock_guard<std::mutex> lock(m_outputMutex);
        m_leftTriggerConfig = m_savedLeftTrigger;
        m_rightTriggerConfig = m_savedRightTrigger;
        m_lightbarR = m_savedLightR;
        m_lightbarG = m_savedLightG;
        m_lightbarB = m_savedLightB;
        m_playerLed = m_savedPlayerLed;
        m_outputDirty = true;
    }
    SendOutputReport();
}

void DualSenseController::TurnOffAll() {
    m_isActive = false;
    {
        std::lock_guard<std::mutex> lock(m_outputMutex);
        m_leftTriggerConfig = DualSenseTriggerConfig{};
        m_rightTriggerConfig = DualSenseTriggerConfig{};
        m_leftRumble = 0;
        m_rightRumble = 0;
        m_rumbleTimer = 0.0f;
        m_leftTriggerPulseTimer = 0.0f;
        m_rightTriggerPulseTimer = 0.0f;
        m_lightbarR = 0;
        m_lightbarG = 0;
        m_lightbarB = 0;
        m_playerLed = DualSensePlayerLed::None;
        m_muteLed = DualSenseMuteLed::Off;
        m_outputDirty = true;
    }
    SendOutputReport();
}

void DualSenseController::Pause() {
    if (!m_isActive) return;
    m_isActive = false;
    {
        std::lock_guard<std::mutex> lock(m_outputMutex);
        m_savedLeftTrigger = (m_leftTriggerPulseTimer > 0.0f) ? m_prePulseLeftTrigger : m_leftTriggerConfig;
        m_savedRightTrigger = (m_rightTriggerPulseTimer > 0.0f) ? m_prePulseRightTrigger : m_rightTriggerConfig;

        // Stop active rumble and trigger motors when unfocused, keep LEDs illuminated
        m_leftTriggerConfig = DualSenseTriggerConfig{};
        m_rightTriggerConfig = DualSenseTriggerConfig{};
        m_leftRumble = 0;
        m_rightRumble = 0;
        m_rumbleTimer = 0.0f;
        m_leftTriggerPulseTimer = 0.0f;
        m_rightTriggerPulseTimer = 0.0f;
        m_outputDirty = true;
    }
    SendOutputReport();
}

void DualSenseController::Resume() {
    m_isActive = true;
    {
        std::lock_guard<std::mutex> lock(m_outputMutex);
        m_leftTriggerConfig = m_savedLeftTrigger;
        m_rightTriggerConfig = m_savedRightTrigger;
        m_lightbarR = m_savedLightR;
        m_lightbarG = m_savedLightG;
        m_lightbarB = m_savedLightB;
        m_playerLed = m_savedPlayerLed;
        m_outputDirty = true;
    }
    SendOutputReport();
}

void DualSenseController::CloseDevice() {
    if (m_deviceHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_deviceHandle);
        m_deviceHandle = INVALID_HANDLE_VALUE;
    }

    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_rawState = DualSenseState{};
    m_currentState = DualSenseState{};
    m_previousState = DualSenseState{};
    m_connectionType = DualSenseConnection::Disconnected;
    m_physicalConnection = DualSenseConnection::Disconnected;
    m_devicePath.clear();
    m_productString.clear();
    m_isEdge = false;
    m_isDualShock4 = false;
}

bool DualSenseController::EnumerateAndOpenDevice() {
    std::vector<std::wstring> candidatePaths;

    // 1. Primary: Windows Configuration Manager (PnP kernel enumeration, immune to Steam overlay HID hooks)
    ULONG bufferSize = 0;
    CONFIGRET cr = CM_Get_Device_Interface_List_SizeW(
        &bufferSize,
        const_cast<GUID*>(&GUID_DEVINTERFACE_HID_CLASS),
        nullptr,
        CM_GET_DEVICE_INTERFACE_LIST_PRESENT
    );

    if (cr == CR_SUCCESS && bufferSize > 1) {
        std::vector<wchar_t> buffer(bufferSize);
        cr = CM_Get_Device_Interface_ListW(
            const_cast<GUID*>(&GUID_DEVINTERFACE_HID_CLASS),
            nullptr,
            buffer.data(),
            bufferSize,
            CM_GET_DEVICE_INTERFACE_LIST_PRESENT
        );
        if (cr == CR_SUCCESS) {
            for (wchar_t* p = buffer.data(); *p; p += wcslen(p) + 1) {
                candidatePaths.emplace_back(p);
            }
        }
    }

    // 2. Secondary Fallback: SetupAPI enumeration
    if (candidatePaths.empty()) {
        GUID hidGuid;
        HidD_GetHidGuid(&hidGuid);

        HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(
            &hidGuid,
            nullptr,
            nullptr,
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
        );

        if (deviceInfoSet != INVALID_HANDLE_VALUE) {
            SP_DEVICE_INTERFACE_DATA interfaceData = {};
            interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

            for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfoSet, nullptr, &hidGuid, i, &interfaceData); ++i) {
                DWORD requiredSize = 0;
                SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &interfaceData, nullptr, 0, &requiredSize, nullptr);
                if (requiredSize == 0) continue;

                std::vector<BYTE> detailDataBuffer(requiredSize);
                PSP_DEVICE_INTERFACE_DETAIL_DATA_W detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailDataBuffer.data());
                detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

                if (SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &interfaceData, detailData, requiredSize, nullptr, nullptr)) {
                    candidatePaths.emplace_back(detailData->DevicePath);
                }
            }
            SetupDiDestroyDeviceInfoList(deviceInfoSet);
        }
    }

    bool found = false;

    for (const auto& devPath : candidatePaths) {
        // Fast pre-filter: device path must contain Sony VID 054C if path is formatted
        std::wstring pathUpper = devPath;
        for (auto& c : pathUpper) c = towupper(c);

        bool pathHasSonyVid = (pathUpper.find(L"VID_054C") != std::wstring::npos || pathUpper.find(L"VID&0002054C") != std::wstring::npos);
        bool pathHasDualSensePid = (pathUpper.find(L"PID_0CE6") != std::wstring::npos || pathUpper.find(L"PID&0CE6") != std::wstring::npos);
        bool pathHasEdgePid = (pathUpper.find(L"PID_0DF2") != std::wstring::npos || pathUpper.find(L"PID&0DF2") != std::wstring::npos);
        bool pathHasDualShock4Pid = (pathUpper.find(L"PID_05C4") != std::wstring::npos || pathUpper.find(L"PID&05C4") != std::wstring::npos ||
                                     pathUpper.find(L"PID_09CC") != std::wstring::npos || pathUpper.find(L"PID&09CC") != std::wstring::npos ||
                                     pathUpper.find(L"PID_0BA0") != std::wstring::npos || pathUpper.find(L"PID&0BA0") != std::wstring::npos);

        // Attempt to open device with Overlapped I/O for high-performance non-blocking reads/writes
        HANDLE handle = CreateFileW(
            devPath.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            nullptr
        );

        if (handle == INVALID_HANDLE_VALUE) {
            // Fallback: Read only / non-exclusive
            handle = CreateFileW(
                devPath.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,
                nullptr
            );
        }

        if (handle == INVALID_HANDLE_VALUE) {
            continue;
        }

        bool isDualSense = false;
        bool isEdge = false;
        bool isDS4 = false;

        HIDD_ATTRIBUTES attributes = {};
        attributes.Size = sizeof(HIDD_ATTRIBUTES);
        if (HidD_GetAttributes(handle, &attributes)) {
            if (attributes.VendorID == SONY_VENDOR_ID) {
                if (attributes.ProductID == DUALSENSE_PRODUCT_ID || attributes.ProductID == DUALSENSE_EDGE_PRODUCT_ID) {
                    isDualSense = true;
                    isEdge = (attributes.ProductID == DUALSENSE_EDGE_PRODUCT_ID);
                } else if (attributes.ProductID == DUALSHOCK4_GEN1_PRODUCT_ID ||
                           attributes.ProductID == DUALSHOCK4_GEN2_PRODUCT_ID ||
                           attributes.ProductID == DUALSHOCK4_DONGLE_PRODUCT_ID) {
                    isDualSense = true;
                    isDS4 = true;
                }
            }
        }

        // Steam Input hook bypass: Steam's overlay hooks HidD_GetAttributes to return FALSE for PlayStation controllers.
        // If HidD_GetAttributes failed, inspect the PnP Hardware ID in the device interface path string.
        if (!isDualSense && pathHasSonyVid && (pathHasDualSensePid || pathHasEdgePid || pathHasDualShock4Pid)) {
            isDualSense = true;
            isEdge = pathHasEdgePid;
            isDS4 = pathHasDualShock4Pid;
        }

        if (isDualSense) {
            m_isDualShock4 = isDS4;
        }

        if (!isDualSense) {
            CloseHandle(handle);
            continue;
        }

        // Inspect HID capabilities to ensure we connect to the primary Gamepad collection
        // (DualSense USB composite devices expose separate endpoints for Gamepad, Audio, and Sensors)
        PHIDP_PREPARSED_DATA preparsedData = nullptr;
        HIDP_CAPS caps = {};
        bool isGamepadInterface = true;

        if (HidD_GetPreparsedData(handle, &preparsedData)) {
            if (HidP_GetCaps(preparsedData, &caps) == HIDP_STATUS_SUCCESS) {
                m_inputReportLength = caps.InputReportByteLength;
                m_outputReportLength = caps.OutputReportByteLength;

                // Check if this interface is a primary gamepad collection
                // Generic Desktop (0x01) with Gamepad (0x05) / Joystick (0x04) / Multi-axis (0x08)
                // Or reports matching 64 (USB Full), 78 (BT Extended), or 10 (BT Basic)
                if (caps.UsagePage != 0x01 && caps.InputReportByteLength < 10) {
                    isGamepadInterface = false;
                }
            }
            HidD_FreePreparsedData(preparsedData);
        }

        if (!isGamepadInterface) {
            CloseHandle(handle);
            continue;
        }

        m_deviceHandle = handle;
        m_isEdge = isEdge;

        // Convert device path to utf8 string
        int strLen = WideCharToMultiByte(CP_UTF8, 0, devPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (strLen > 0) {
            m_devicePath.resize(strLen - 1);
            WideCharToMultiByte(CP_UTF8, 0, devPath.c_str(), -1, &m_devicePath[0], strLen, nullptr, nullptr);
        }

        wchar_t prodStr[128] = {};
        if (HidD_GetProductString(handle, prodStr, sizeof(prodStr))) {
            m_productString = prodStr;
        } else {
            m_productString = m_isEdge ? L"DualSense Edge Wireless Controller" : L"DualSense Wireless Controller";
        }

        // Robust connection type detection via device path markers and HID report length.
        // Windows BT HID paths always start with \\?\BTH or contain BTHLE.
        // USB paths contain "usb" and may have composite sub-interfaces "&col".
        // Fallback: extended BT report is 78 bytes, USB is 64 bytes.
        std::string pathLower = m_devicePath;
        std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::tolower);

        bool pathIsBluetooth = (pathLower.find("00001124") != std::string::npos ||
                                pathLower.find("0002054c") != std::string::npos ||
                                pathLower.find("bth") != std::string::npos ||
                                pathLower.find("bluetooth") != std::string::npos);
        bool pathIsUsb       = (!pathIsBluetooth && (pathLower.find("usb") != std::string::npos ||
                                                      pathLower.find("&col") != std::string::npos ||
                                                      pathLower.find("mi_00") != std::string::npos));
        bool reportLenIsBt   = (m_inputReportLength >= 78 || m_outputReportLength > 64);
        bool reportLenIsUsb  = (m_inputReportLength > 0 && m_inputReportLength <= 64 && m_outputReportLength <= 64);

        if (pathIsBluetooth || (!pathIsUsb && reportLenIsBt)) {
            m_physicalConnection = DualSenseConnection::Bluetooth;
        } else {
            m_physicalConnection = DualSenseConnection::USB;
        }
        m_connectionType = m_physicalConnection;

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_rawState.connection = m_physicalConnection;
            m_currentState.connection = m_physicalConnection;
        }

        m_outputDirty = true;
        found = true;
        break;
    }

    return found;
}

void DualSenseController::DeviceWorkerThread() {
    uint8_t inputBuffer[548] = {};
    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    while (m_running.load()) {
        if (m_deviceHandle == INVALID_HANDLE_VALUE) {
            if (!EnumerateAndOpenDevice()) {
                // Secondary check: XInput Gamepad (Steam Input or Xbox / DS4Windows emulator)
                XINPUT_STATE xstate = {};
                if (XInputGetState(0, &xstate) == ERROR_SUCCESS) {
                    DualSenseState newState = {};
                    newState.connection = DualSenseConnection::XInput;

                    auto normStick = [](SHORT val, SHORT deadzone) -> float {
                        if (std::abs(val) <= deadzone) return 0.0f;
                        float f = static_cast<float>(val) / 32767.0f;
                        return (std::clamp)(f, -1.0f, 1.0f);
                    };
                    newState.leftStickX = normStick(xstate.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
                    newState.leftStickY = normStick(xstate.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
                    newState.rightStickX = normStick(xstate.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
                    newState.rightStickY = normStick(xstate.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);

                    newState.leftTrigger = (xstate.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) ? (xstate.Gamepad.bLeftTrigger / 255.0f) : 0.0f;
                    newState.rightTrigger = (xstate.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) ? (xstate.Gamepad.bRightTrigger / 255.0f) : 0.0f;

                    WORD xb = xstate.Gamepad.wButtons;
                    uint32_t btns = 0;
                    if (xb & XINPUT_GAMEPAD_A) btns |= static_cast<uint32_t>(DualSenseButton::Cross);
                    if (xb & XINPUT_GAMEPAD_B) btns |= static_cast<uint32_t>(DualSenseButton::Circle);
                    if (xb & XINPUT_GAMEPAD_X) btns |= static_cast<uint32_t>(DualSenseButton::Square);
                    if (xb & XINPUT_GAMEPAD_Y) btns |= static_cast<uint32_t>(DualSenseButton::Triangle);
                    if (xb & XINPUT_GAMEPAD_DPAD_UP) btns |= static_cast<uint32_t>(DualSenseButton::DPadUp);
                    if (xb & XINPUT_GAMEPAD_DPAD_DOWN) btns |= static_cast<uint32_t>(DualSenseButton::DPadDown);
                    if (xb & XINPUT_GAMEPAD_DPAD_LEFT) btns |= static_cast<uint32_t>(DualSenseButton::DPadLeft);
                    if (xb & XINPUT_GAMEPAD_DPAD_RIGHT) btns |= static_cast<uint32_t>(DualSenseButton::DPadRight);
                    if (xb & XINPUT_GAMEPAD_LEFT_SHOULDER) btns |= static_cast<uint32_t>(DualSenseButton::L1);
                    if (xb & XINPUT_GAMEPAD_RIGHT_SHOULDER) btns |= static_cast<uint32_t>(DualSenseButton::R1);
                    if (xb & XINPUT_GAMEPAD_LEFT_THUMB) btns |= static_cast<uint32_t>(DualSenseButton::L3);
                    if (xb & XINPUT_GAMEPAD_RIGHT_THUMB) btns |= static_cast<uint32_t>(DualSenseButton::R3);
                    if (xb & XINPUT_GAMEPAD_START) btns |= static_cast<uint32_t>(DualSenseButton::Options);
                    if (xb & XINPUT_GAMEPAD_BACK) btns |= static_cast<uint32_t>(DualSenseButton::Create);
                    if (newState.leftTrigger > 0.5f) btns |= static_cast<uint32_t>(DualSenseButton::L2Button);
                    if (newState.rightTrigger > 0.5f) btns |= static_cast<uint32_t>(DualSenseButton::R2Button);
                    newState.buttons = btns;
                    newState.battery.level = 100;
                    newState.battery.isFull = true;

                    {
                        std::lock_guard<std::mutex> lock(m_stateMutex);
                        m_rawState = newState;
                    }

                    if (m_outputDirty) {
                        SendOutputReport();
                    }

                    Sleep(10);
                    continue;
                }

                // Controller not found or disconnected, retry after 2000ms
                for (int i = 0; i < 40 && m_running.load(); ++i) {
                    Sleep(50);
                }
                continue;
            }

            // Device opened: apply active profile and lightbar/LEDs
            {
                std::lock_guard<std::mutex> lock(m_outputMutex);
                m_isActive = true;
                m_leftTriggerConfig = m_savedLeftTrigger;
                m_rightTriggerConfig = m_savedRightTrigger;
                m_lightbarR = m_savedLightR;
                m_lightbarG = m_savedLightG;
                m_lightbarB = m_savedLightB;
                m_playerLed = m_savedPlayerLed;
                m_outputDirty = true;
            }
            SendOutputReport();
        }

        ResetEvent(overlapped.hEvent);
        memset(inputBuffer, 0, sizeof(inputBuffer));

        DWORD bytesToRead = (m_inputReportLength > 0) ? (std::max)(static_cast<DWORD>(m_inputReportLength), static_cast<DWORD>(64)) : sizeof(inputBuffer);
        if (bytesToRead > sizeof(inputBuffer)) bytesToRead = sizeof(inputBuffer);

        DWORD bytesRead = 0;
        BOOL readSuccess = ReadFile(
            m_deviceHandle,
            inputBuffer,
            bytesToRead,
            &bytesRead,
            &overlapped
        );

        if (!readSuccess) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                // Wait for read completion or timeout / cancellation
                DWORD waitRes = WaitForSingleObject(overlapped.hEvent, 100);
                if (waitRes == WAIT_OBJECT_0) {
                    if (GetOverlappedResult(m_deviceHandle, &overlapped, &bytesRead, FALSE)) {
                        readSuccess = TRUE;
                    } else {
                        DWORD ovErr = GetLastError();
                        if (ovErr == ERROR_DEVICE_NOT_CONNECTED || ovErr == ERROR_GEN_FAILURE || ovErr == ERROR_FILE_NOT_FOUND) {
                            CloseDevice();
                            continue;
                        }
                    }
                } else if (waitRes == WAIT_TIMEOUT) {
                    CancelIoEx(m_deviceHandle, &overlapped);
                    DWORD dummy = 0;
                    GetOverlappedResult(m_deviceHandle, &overlapped, &dummy, TRUE);
                    // Do not continue here: proceed below so periodic heartbeat and pending m_outputDirty are sent!
                } else {
                    CloseDevice();
                    continue;
                }
            } else if (err == ERROR_DEVICE_NOT_CONNECTED || err == ERROR_GEN_FAILURE || err == ERROR_FILE_NOT_FOUND || err == ERROR_ACCESS_DENIED) {
                // Device disconnected
                CloseDevice();
                continue;
            } else {
                // Transient error, sleep briefly and retry
                Sleep(5);
                continue;
            }
        }

        if (readSuccess && bytesRead > 0) {
            ParseInputReport(inputBuffer, bytesRead);
        }

        // Periodic heartbeat output report (refreshes Bluetooth LED watchdog and trigger effects every 250ms)
        static auto lastHeartbeat = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        int heartbeatInterval = (m_keepColorInBackground && !m_isActive) ? 100 : 250;
        if ((m_isActive || m_keepColorInBackground) && std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHeartbeat).count() >= heartbeatInterval) {
            lastHeartbeat = now;
            m_outputDirty = true;
        }

        // Send output reports if dirty
        if (m_outputDirty && (m_isActive || m_keepColorInBackground)) {
            SendOutputReport();
        }
    }

    if (overlapped.hEvent) {
        CloseHandle(overlapped.hEvent);
    }
}

void DualSenseController::ParseInputReport(const uint8_t* data, size_t size) {
    if (!data || size < 9) return;

    DualSenseState newState = {};

    uint8_t reportId = data[0];
    const uint8_t* reportData = data;

    uint8_t rawStickLX = 128, rawStickLY = 128;
    uint8_t rawStickRX = 128, rawStickRY = 128;
    uint8_t rawL2 = 0, rawR2 = 0;
    uint8_t dpadAndShapes = 0;
    uint8_t btnByte2 = 0;
    uint8_t btnByte3 = 0;
    int imuOffset = -1;
    int touchOffset = -1;
    int batteryOffset = -1;

    if (m_isDualShock4 && reportId == 0x11 && size >= 12) {
        // DualShock 4 Extended Bluetooth Report (0x11, typically 78 bytes)
        m_physicalConnection = DualSenseConnection::Bluetooth;
        newState.connection = DualSenseConnection::Bluetooth;
        rawStickLX = reportData[3];
        rawStickLY = reportData[4];
        rawStickRX = reportData[5];
        rawStickRY = reportData[6];
        dpadAndShapes = reportData[7];
        btnByte2 = reportData[8];
        btnByte3 = reportData[9];
        rawL2 = reportData[10];
        rawR2 = reportData[11];
        imuOffset = 15;
        touchOffset = 37;
        batteryOffset = 32;
    } else if (m_isDualShock4 && reportId == 0x01 && size >= 10) {
        // DualShock 4 USB Report (64 bytes) or Basic Bluetooth Report (10 bytes)
        if (size >= 64) {
            m_physicalConnection = DualSenseConnection::USB;
            newState.connection = DualSenseConnection::USB;
            imuOffset = 13;
            touchOffset = 35;
            batteryOffset = 30;
        } else {
            m_physicalConnection = DualSenseConnection::Bluetooth;
            newState.connection = DualSenseConnection::Bluetooth;
        }
        rawStickLX = reportData[1];
        rawStickLY = reportData[2];
        rawStickRX = reportData[3];
        rawStickRY = reportData[4];
        dpadAndShapes = reportData[5];
        btnByte2 = reportData[6];
        btnByte3 = reportData[7];
        rawL2 = reportData[8];
        rawR2 = reportData[9];
    } else if (reportId == 0x11 && size >= 12) {
        // DualShock 4 Extended Bluetooth Report (0x11, typically 78 bytes)
        m_physicalConnection = DualSenseConnection::Bluetooth;
        newState.connection = DualSenseConnection::Bluetooth;
        m_isDualShock4 = true;
        rawStickLX = reportData[2];
        rawStickLY = reportData[3];
        rawStickRX = reportData[4];
        rawStickRY = reportData[5];
        dpadAndShapes = reportData[6];
        btnByte2 = reportData[7];
        btnByte3 = reportData[8];
        rawL2 = reportData[9];
        rawR2 = reportData[10];
        imuOffset = 14;
        touchOffset = 36;
        batteryOffset = 31;
    } else if (reportId == 0x31 && size >= 12) {
        // Extended Bluetooth Report (0x31, typically 78 bytes)
        m_physicalConnection = DualSenseConnection::Bluetooth;
        newState.connection = DualSenseConnection::Bluetooth;
        rawStickLX = reportData[2];
        rawStickLY = reportData[3];
        rawStickRX = reportData[4];
        rawStickRY = reportData[5];
        rawL2 = reportData[6];
        rawR2 = reportData[7];
        dpadAndShapes = reportData[9];
        btnByte2 = reportData[10];
        btnByte3 = reportData[11];
        imuOffset = 17;
        touchOffset = 34;
        batteryOffset = 54;
    } else if (reportId == 0x01 && size >= 64) {
        // Full USB Input Report (0x01, 64 bytes)
        if (m_physicalConnection != DualSenseConnection::Bluetooth) {
            m_physicalConnection = DualSenseConnection::USB;
            newState.connection = DualSenseConnection::USB;
        } else {
            newState.connection = DualSenseConnection::Bluetooth;
        }
        rawStickLX = reportData[1];
        rawStickLY = reportData[2];
        rawStickRX = reportData[3];
        rawStickRY = reportData[4];
        rawL2 = reportData[5];
        rawR2 = reportData[6];
        dpadAndShapes = reportData[8];
        btnByte2 = reportData[9];
        btnByte3 = reportData[10];
        imuOffset = 16;
        touchOffset = 33;
        batteryOffset = 53;
    } else if (reportId == 0x01 && size >= 10) {
        // Basic Bluetooth Input Report (0x01, 10 bytes)
        // In basic BT mode: Byte 5 is Buttons 1 (DPad & Shapes), Byte 6 is Buttons 2, Byte 7 is Buttons 3, Bytes 8 & 9 are L2 & R2!
        m_physicalConnection = DualSenseConnection::Bluetooth;
        newState.connection = DualSenseConnection::Bluetooth;
        rawStickLX = reportData[1];
        rawStickLY = reportData[2];
        rawStickRX = reportData[3];
        rawStickRY = reportData[4];
        dpadAndShapes = reportData[5];
        btnByte2 = reportData[6];
        btnByte3 = reportData[7];
        rawL2 = reportData[8];
        rawR2 = reportData[9];
        // Trigger output report to switch controller to extended 0x31 report mode!
        m_outputDirty = true;
    } else if (size >= 9 && reportId != 0x01 && reportId != 0x31) {
        // Raw Input report (Report ID stripped by OS driver)
        newState.connection = m_physicalConnection;
        rawStickLX = reportData[0];
        rawStickLY = reportData[1];
        rawStickRX = reportData[2];
        rawStickRY = reportData[3];
        if (size >= 63) {
            rawL2 = reportData[4];
            rawR2 = reportData[5];
            dpadAndShapes = reportData[7];
            btnByte2 = reportData[8];
            btnByte3 = reportData[9];
            imuOffset = 15;
            touchOffset = 32;
            batteryOffset = 52;
        } else {
            dpadAndShapes = reportData[4];
            btnByte2 = reportData[5];
            btnByte3 = reportData[6];
            rawL2 = reportData[7];
            rawR2 = reportData[8];
        }
    } else {
        return;
    }

    m_connectionType = m_physicalConnection;

    // 1. Analog Sticks (0..255, center 128) -> normalized [-1.0f, +1.0f]
    float rawLX = (static_cast<float>(rawStickLX) - 128.0f) / 127.5f;
    float rawLY = -(static_cast<float>(rawStickLY) - 128.0f) / 127.5f;
    float rawRX = (static_cast<float>(rawStickRX) - 128.0f) / 127.5f;
    float rawRY = -(static_cast<float>(rawStickRY) - 128.0f) / 127.5f;

    ApplyDeadzone(rawLX, rawLY, m_stickDeadzone, newState.leftStickX, newState.leftStickY);
    ApplyDeadzone(rawRX, rawRY, m_stickDeadzone, newState.rightStickX, newState.rightStickY);

    // 2. Analog Triggers (0..255) -> normalized [0.0f, 1.0f]
    float fL2 = static_cast<float>(rawL2) / 255.0f;
    float fR2 = static_cast<float>(rawR2) / 255.0f;
    newState.leftTrigger = (fL2 > m_triggerDeadzone) ? (fL2 - m_triggerDeadzone) / (1.0f - m_triggerDeadzone) : 0.0f;
    newState.rightTrigger = (fR2 > m_triggerDeadzone) ? (fR2 - m_triggerDeadzone) / (1.0f - m_triggerDeadzone) : 0.0f;

    // 3. D-Pad and Shape Buttons
    uint8_t dpad = dpadAndShapes & 0x0F;
    uint32_t buttons = 0;
    switch (dpad) {
        case 0: buttons |= DualSenseButton::DPadUp; break;
        case 1: buttons |= DualSenseButton::DPadUp | DualSenseButton::DPadRight; break;
        case 2: buttons |= DualSenseButton::DPadRight; break;
        case 3: buttons |= DualSenseButton::DPadDown | DualSenseButton::DPadRight; break;
        case 4: buttons |= DualSenseButton::DPadDown; break;
        case 5: buttons |= DualSenseButton::DPadDown | DualSenseButton::DPadLeft; break;
        case 6: buttons |= DualSenseButton::DPadLeft; break;
        case 7: buttons |= DualSenseButton::DPadUp | DualSenseButton::DPadLeft; break;
        default: break; // 8 = released
    }

    if (dpadAndShapes & 0x10) buttons |= DualSenseButton::Square;
    if (dpadAndShapes & 0x20) buttons |= DualSenseButton::Cross;
    if (dpadAndShapes & 0x40) buttons |= DualSenseButton::Circle;
    if (dpadAndShapes & 0x80) buttons |= DualSenseButton::Triangle;

    // 4. Shoulder, System & Stick Buttons
    if (btnByte2 & 0x01) buttons |= DualSenseButton::L1;
    if (btnByte2 & 0x02) buttons |= DualSenseButton::R1;
    if (btnByte2 & 0x04) buttons |= DualSenseButton::L2Button;
    if (btnByte2 & 0x08) buttons |= DualSenseButton::R2Button;
    if (btnByte2 & 0x10) buttons |= DualSenseButton::Create;
    if (btnByte2 & 0x20) buttons |= DualSenseButton::Options;
    if (btnByte2 & 0x40) buttons |= DualSenseButton::L3;
    if (btnByte2 & 0x80) buttons |= DualSenseButton::R3;

    // 5. PS, Touchpad Click & Mic Mute
    if (btnByte3 & 0x01) buttons |= DualSenseButton::PS;
    if (btnByte3 & 0x02) buttons |= DualSenseButton::Touchpad;
    if (btnByte3 & 0x04) buttons |= DualSenseButton::MicMute;

    newState.buttons = buttons;

    // 6. Motion / IMU Sensors (Gyroscope & Accelerometer)
    if (imuOffset >= 0 && size >= static_cast<size_t>(imuOffset + 12)) {
        int16_t gyroPitchRaw = static_cast<int16_t>(reportData[imuOffset + 0] | (reportData[imuOffset + 1] << 8));
        int16_t gyroYawRaw   = static_cast<int16_t>(reportData[imuOffset + 2] | (reportData[imuOffset + 3] << 8));
        int16_t gyroRollRaw  = static_cast<int16_t>(reportData[imuOffset + 4] | (reportData[imuOffset + 5] << 8));

        int16_t accelXRaw = static_cast<int16_t>(reportData[imuOffset + 6] | (reportData[imuOffset + 7] << 8));
        int16_t accelYRaw = static_cast<int16_t>(reportData[imuOffset + 8] | (reportData[imuOffset + 9] << 8));
        int16_t accelZRaw = static_cast<int16_t>(reportData[imuOffset + 10] | (reportData[imuOffset + 11] << 8));

        // Gyro scale factor (~2000 deg/s range over 32767 values)
        const float gyroScale = 2000.0f / 32767.0f;
        // Accel scale factor (~4G range over 32767 values -> 1G ≈ 8192)
        const float accelScale = 1.0f / 8192.0f;

        float gPitch = static_cast<float>(gyroPitchRaw) * gyroScale;
        float gYaw   = static_cast<float>(gyroYawRaw)   * gyroScale;
        float gRoll  = static_cast<float>(gyroRollRaw)  * gyroScale;

        if (m_calibrating) {
            m_gyroBiasPitch += gPitch;
            m_gyroBiasYaw   += gYaw;
            m_gyroBiasRoll  += gRoll;
            m_calibrationSamples++;
            if (m_calibrationSamples >= 200) {
                m_gyroBiasPitch /= static_cast<float>(m_calibrationSamples);
                m_gyroBiasYaw   /= static_cast<float>(m_calibrationSamples);
                m_gyroBiasRoll  /= static_cast<float>(m_calibrationSamples);
                m_calibrating = false;
            }
        }

        newState.motion.gyroPitch = gPitch - m_gyroBiasPitch;
        newState.motion.gyroYaw   = gYaw   - m_gyroBiasYaw;
        newState.motion.gyroRoll  = gRoll  - m_gyroBiasRoll;

        newState.motion.accelX = static_cast<float>(accelXRaw) * accelScale;
        newState.motion.accelY = static_cast<float>(accelYRaw) * accelScale;
        newState.motion.accelZ = static_cast<float>(accelZRaw) * accelScale;

        if (size >= static_cast<size_t>(imuOffset + 16)) {
            newState.motion.sensorTimestamp = reportData[imuOffset + 12] | (reportData[imuOffset + 13] << 8) |
                                              (reportData[imuOffset + 14] << 16) | (reportData[imuOffset + 15] << 24);
        }
    }

    // 7. Multi-touch Touchpad (2 points)
    if (touchOffset >= 0 && size >= static_cast<size_t>(touchOffset + 8)) {
        // Touch Point 0
        uint8_t t0Header = reportData[touchOffset + 0];
        newState.touch[0].active = (t0Header & 0x80) == 0;
        newState.touch[0].id = t0Header & 0x7F;
        uint32_t t0RawX = reportData[touchOffset + 1] | ((reportData[touchOffset + 2] & 0x0F) << 8);
        uint32_t t0RawY = ((reportData[touchOffset + 2] & 0xF0) >> 4) | (reportData[touchOffset + 3] << 4);
        newState.touch[0].x = static_cast<float>(t0RawX) / 1920.0f;
        newState.touch[0].y = static_cast<float>(t0RawY) / 1080.0f;

        // Touch Point 1
        uint8_t t1Header = reportData[touchOffset + 4];
        newState.touch[1].active = (t1Header & 0x80) == 0;
        newState.touch[1].id = t1Header & 0x7F;
        uint32_t t1RawX = reportData[touchOffset + 5] | ((reportData[touchOffset + 6] & 0x0F) << 8);
        uint32_t t1RawY = ((reportData[touchOffset + 6] & 0xF0) >> 4) | (reportData[touchOffset + 7] << 4);
        newState.touch[1].x = static_cast<float>(t1RawX) / 1920.0f;
        newState.touch[1].y = static_cast<float>(t1RawY) / 1080.0f;
    }

    // 8. Battery Telemetry
    if (batteryOffset >= 0 && size >= static_cast<size_t>(batteryOffset + 1)) {
        uint8_t bByte = reportData[batteryOffset];
        uint8_t bLevel = (bByte & 0x0F);
        newState.battery.level = (std::min)(static_cast<uint8_t>(bLevel * 10), static_cast<uint8_t>(100));
        uint8_t bStatus = (bByte >> 4) & 0x0F;
        newState.battery.isCharging = (bStatus == 0x01);
        newState.battery.isFull = (bStatus == 0x02 || bLevel >= 10);
    }

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_rawState = newState;
    }
}

void DualSenseController::SendOutputReport() {
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        if (m_rawState.connection == DualSenseConnection::XInput) {
            XINPUT_VIBRATION vib = {};
            vib.wLeftMotorSpeed = static_cast<WORD>(m_leftRumble * 257);
            vib.wRightMotorSpeed = static_cast<WORD>(m_rightRumble * 257);
            XInputSetState(0, &vib);
            m_outputDirty = false;
            return;
        }
    }

    if (m_deviceHandle == INVALID_HANDLE_VALUE) return;

    std::lock_guard<std::mutex> lock(m_outputMutex);

    uint8_t buffer[1024] = {};
    DWORD bytesToWrite = 0;
    DWORD bytesWritten = 0;

    bool isBluetooth = (m_physicalConnection == DualSenseConnection::Bluetooth || m_connectionType == DualSenseConnection::Bluetooth);

    // Encode trigger effects into hardware parameter arrays
    auto BitpackTriggerZones = [](const uint8_t strengthArray[10], uint8_t frequency, uint8_t mode, uint8_t& outMode, uint8_t outParams[10]) {
        uint32_t strength_zones = 0;
        uint16_t active_zones = 0;
        for (int i = 0; i < 10; ++i) {
            uint8_t str = strengthArray[i];
            if (str > 8) str = 8;
            if (str > 0) {
                uint8_t val = static_cast<uint8_t>((str - 1) & 0x07);
                strength_zones |= (static_cast<uint32_t>(val) << (3 * i));
                active_zones |= static_cast<uint16_t>(1 << i);
            }
        }
        outMode = mode;
        outParams[0] = static_cast<uint8_t>(active_zones & 0xFF);
        outParams[1] = static_cast<uint8_t>((active_zones >> 8) & 0xFF);
        outParams[2] = static_cast<uint8_t>(strength_zones & 0xFF);
        outParams[3] = static_cast<uint8_t>((strength_zones >> 8) & 0xFF);
        outParams[4] = static_cast<uint8_t>((strength_zones >> 16) & 0xFF);
        outParams[5] = static_cast<uint8_t>((strength_zones >> 24) & 0xFF);
        outParams[6] = 0;
        outParams[7] = 0;
        outParams[8] = frequency;
        outParams[9] = 0;
    };

    auto EncodeTrigger = [&](const DualSenseTriggerConfig& config, uint8_t& outMode, uint8_t outParams[10]) {
        memset(outParams, 0, 10);
        switch (config.effect) {
            case DualSenseTriggerEffect::None: {
                outMode = 0x05; // Explicit hardware trigger OFF / motor reset (0x05)
                break;
            }
            case DualSenseTriggerEffect::ContinuousResistance: {
                // Continuous uniform resistance from startPosition to max
                uint8_t startPos = (std::clamp)(static_cast<int>((config.startPosition / 255.0f) * 9.0f), 0, 9);
                uint8_t strength = (std::clamp)(static_cast<int>((config.force / 255.0f) * 7.0f + 1.0f), 1, 8);
                uint8_t arr[10] = {};
                for (int i = startPos; i < 10; ++i) {
                    arr[i] = strength;
                }
                BitpackTriggerZones(arr, 0, 0x21, outMode, outParams); // Mode 0x21: DS_TRIGGER_EFFECT_FEEDBACK
                break;
            }
            case DualSenseTriggerEffect::SectionResistance: {
                // Resistance in a specific section [startPosition, endPosition]
                uint8_t startPos = (std::clamp)(static_cast<int>((config.startPosition / 255.0f) * 9.0f), 0, 9);
                uint8_t endPos = (std::clamp)(static_cast<int>((config.endPosition / 255.0f) * 9.0f), static_cast<int>(startPos), 9);
                uint8_t strength = (std::clamp)(static_cast<int>((config.force / 255.0f) * 7.0f + 1.0f), 1, 8);
                uint8_t arr[10] = {};
                for (int i = startPos; i <= endPos; ++i) {
                    arr[i] = strength;
                }
                BitpackTriggerZones(arr, 0, 0x21, outMode, outParams); // Mode 0x21: DS_TRIGGER_EFFECT_FEEDBACK
                break;
            }
            case DualSenseTriggerEffect::GallopingWeapon: {
                // Galloping / Machine gun recoil effect (Mode 0x23)
                uint8_t startPos = (std::clamp)(static_cast<int>((config.startPosition / 255.0f) * 8.0f), 0, 8);
                uint8_t endPos = (std::clamp)(static_cast<int>((config.endPosition / 255.0f) * 9.0f), startPos + 1, 9);
                uint8_t firstFoot = (std::clamp)(static_cast<int>((config.force / 255.0f) * 6.0f), 0, 6);
                uint8_t secondFoot = (std::clamp)(firstFoot + 1, 1, 7);
                uint8_t freq = (std::clamp)(static_cast<int>((config.frequency / 255.0f) * 7.0f + 1.0f), 1, 8);
                uint16_t startStop = static_cast<uint16_t>((1 << startPos) | (1 << endPos));
                uint32_t ratio = static_cast<uint32_t>((secondFoot & 0x07) | ((firstFoot & 0x07) << 3));
                outMode = 0x23; // DS_TRIGGER_EFFECT_GALLOPING
                outParams[0] = static_cast<uint8_t>(startStop & 0xFF);
                outParams[1] = static_cast<uint8_t>((startStop >> 8) & 0xFF);
                outParams[2] = static_cast<uint8_t>(ratio & 0xFF);
                outParams[3] = freq;
                break;
            }
            case DualSenseTriggerEffect::Bow: {
                // Bow tension effect (Mode 0x22)
                uint8_t startPos = (std::clamp)(static_cast<int>((config.startPosition / 255.0f) * 8.0f), 0, 8);
                uint8_t endPos = (std::clamp)(static_cast<int>((config.endPosition / 255.0f) * 8.0f), startPos + 1, 8);
                uint8_t str = (std::clamp)(static_cast<int>((config.force / 255.0f) * 7.0f + 1.0f), 1, 8);
                uint8_t snap = (std::clamp)(static_cast<int>((config.extraForce / 255.0f) * 7.0f + 1.0f), 1, 8);
                uint16_t startStop = static_cast<uint16_t>((1 << startPos) | (1 << endPos));
                uint32_t forcePair = static_cast<uint32_t>(((str - 1) & 0x07) | (((snap - 1) & 0x07) << 3));
                outMode = 0x22; // DS_TRIGGER_EFFECT_BOW
                outParams[0] = static_cast<uint8_t>(startStop & 0xFF);
                outParams[1] = static_cast<uint8_t>((startStop >> 8) & 0xFF);
                outParams[2] = static_cast<uint8_t>(forcePair & 0xFF);
                break;
            }
            case DualSenseTriggerEffect::RigidStop: {
                // Hard weapon wall break / stop (Mode 0x25)
                uint8_t startPos = (std::clamp)(static_cast<int>((config.startPosition / 255.0f) * 5.0f + 2.0f), 2, 7);
                uint8_t endPos = (std::clamp)(startPos + 1, 3, 8);
                uint8_t str = (std::clamp)(static_cast<int>((config.force / 255.0f) * 7.0f + 1.0f), 1, 8);
                uint16_t startStop = static_cast<uint16_t>((1 << startPos) | (1 << endPos));
                outMode = 0x25; // DS_TRIGGER_EFFECT_WEAPON
                outParams[0] = static_cast<uint8_t>(startStop & 0xFF);
                outParams[1] = static_cast<uint8_t>((startStop >> 8) & 0xFF);
                outParams[2] = static_cast<uint8_t>(str - 1);
                break;
            }
            case DualSenseTriggerEffect::Vibration: {
                // Tactile Trigger Vibration effect (Mode 0x26: DS_TRIGGER_EFFECT_VIBRATION)
                uint8_t startPos = (config.startPosition <= 9 && config.startPosition > 0) ? config.startPosition : (std::clamp)(static_cast<int>((config.startPosition / 255.0f) * 9.0f), 0, 9);
                uint8_t endPos = (config.endPosition <= 9 && config.endPosition > 0) ? config.endPosition : (std::clamp)(static_cast<int>((config.endPosition / 255.0f) * 9.0f), static_cast<int>(startPos), 9);
                if (config.startPosition == 0 && config.endPosition == 0) {
                    endPos = 9; // Full trigger range if unspecified
                }
                uint8_t amp = (config.force == 0) ? 0 : ((config.force <= 8) ? config.force : (std::clamp)(static_cast<int>((config.force / 255.0f) * 7.0f + 1.0f), 1, 8));
                uint8_t freq = (config.frequency > 0) ? config.frequency : 40;
                uint8_t arr[10] = {};
                if (amp > 0) {
                    for (int i = startPos; i <= endPos; ++i) {
                        arr[i] = amp;
                    }
                }
                BitpackTriggerZones(arr, freq, 0x26, outMode, outParams); // Mode 0x26: DS_TRIGGER_EFFECT_VIBRATION
                break;
            }
            case DualSenseTriggerEffect::Custom: {
                outMode = config.mode;
                memcpy(outParams, config.rawParams, 10);
                break;
            }
        }
    };

    uint8_t leftTriggerMode = 0;
    uint8_t leftTriggerParams[10] = {};
    EncodeTrigger(m_leftTriggerConfig, leftTriggerMode, leftTriggerParams);

    uint8_t rightTriggerMode = 0;
    uint8_t rightTriggerParams[10] = {};
    EncodeTrigger(m_rightTriggerConfig, rightTriggerMode, rightTriggerParams);

    if (m_isDualShock4) {
        if (!isBluetooth) {
            // DualShock 4 USB Output Report (Report 0x05, 32 bytes)
            buffer[0] = 0x05;
            buffer[1] = 0xFF; // Enable rumble and lightbar
            buffer[2] = 0x00;
            buffer[3] = 0x00;
            buffer[4] = m_rightRumble; // Small fast motor
            buffer[5] = m_leftRumble;  // Heavy slow motor
            buffer[6] = m_lightbarR;
            buffer[7] = m_lightbarG;
            buffer[8] = m_lightbarB;
            bytesToWrite = 32;
        } else {
            // DualShock 4 Bluetooth Output Report (Report 0x11, 78 bytes)
            buffer[0] = 0x11;
            buffer[1] = 0xC0;
            buffer[2] = 0x20;
            buffer[3] = 0x0F; // Enable rumble & lightbar
            buffer[6] = m_rightRumble;
            buffer[7] = m_leftRumble;
            buffer[8] = m_lightbarR;
            buffer[9] = m_lightbarG;
            buffer[10] = m_lightbarB;

            uint8_t crcPayload[75] = {};
            crcPayload[0] = 0xA2;
            memcpy(&crcPayload[1], buffer, 74);
            uint32_t crc = CalculateCRC32(crcPayload, 75);
            buffer[74] = static_cast<uint8_t>(crc & 0xFF);
            buffer[75] = static_cast<uint8_t>((crc >> 8) & 0xFF);
            buffer[76] = static_cast<uint8_t>((crc >> 16) & 0xFF);
            buffer[77] = static_cast<uint8_t>((crc >> 24) & 0xFF);
            bytesToWrite = 78;
        }
    } else if (!isBluetooth) {
        // USB Output Report (Report ID 0x02, 64 bytes total on Windows)
        buffer[0] = 0x02; // Report ID
        buffer[1] = 0xFF; // Valid flag 0 (Enable motors, triggers, audio volume & routing)
        buffer[2] = 0xFF; // Valid flag 1: 0xFF (RELEASE_LEDS 0x08 | LIGHTBAR_ENABLE 0x20 | PLAYER_LED_ENABLE 0x10 | LIGHTBAR_SETUP 0x04 | MUTE_LED 0x01 | AUDIO2 0x80)
        buffer[3] = m_rightRumble; // High-frequency weak motor
        buffer[4] = m_leftRumble;  // Low-frequency strong motor
        buffer[5] = m_headphoneVolume; // Headphone volume (default 0x7F = 100%)
        buffer[6] = m_speakerVolume;   // Internal speaker volume (default 0x64 = 100%)
        buffer[7] = 0x00; // Mic volume
        buffer[8] = m_audioRouting;    // Audio flags: 0x30 = route to internal speaker (unmute!)
        buffer[9] = static_cast<uint8_t>(m_muteLed);
        buffer[10] = 0x00; // Power save control

        // Right Trigger (R2) at byte 11
        buffer[11] = rightTriggerMode;
        memcpy(&buffer[12], rightTriggerParams, 10);

        // Left Trigger (L2) at byte 22
        buffer[22] = leftTriggerMode;
        memcpy(&buffer[23], leftTriggerParams, 10);

        // Audio Control 2 (Preamp gain) at byte 38
        buffer[38] = 0x02; // +6dB preamp gain boost for internal speaker

        // Valid flag 2 at byte 39
        buffer[39] = 0x07; // Valid flag 2: enable LED brightness (0x01) | lightbar setup (0x02) | compatible vibration v2 (0x04)

        // Lightbar setup & Player LEDs
        buffer[42] = 0x02; // 0x02 = DS_OUTPUT_LIGHTBAR_SETUP_LIGHT_OUT (Disables boot fade sequence so host RGB takes effect immediately)
        buffer[43] = 0x00; // LED brightness (0 = High)
        buffer[44] = static_cast<uint8_t>(m_playerLed) | 0x20; // 0x20 = instant player LED hardware update bit
        buffer[45] = m_lightbarR; // Red
        buffer[46] = m_lightbarG; // Green
        buffer[47] = m_lightbarB; // Blue

        bytesToWrite = 64;
    } else {
        // Bluetooth Output Report (Report ID 0x31, 78 bytes)
        static uint8_t s_outputSeq = 0;
        buffer[0] = 0x31; // Report ID
        buffer[1] = (s_outputSeq << 4) | 0x00; // Sequence tag (high nibble: seq 0..15, low nibble: 0)
        s_outputSeq = (s_outputSeq + 1) & 0x0F;
        buffer[2] = 0x10; // Magic DS_OUTPUT_TAG required by DualSense BT firmware
        buffer[3] = 0xFF; // Valid flag 0: enable rumble, triggers, audio volume & routing
        buffer[4] = 0x57; // Valid flag 1: 0x57 (RELEASE_LEDS 0x08 | LIGHTBAR_ENABLE 0x04 | PLAYER_LED_ENABLE 0x10 | MIC_MUTE_LED 0x01 | POWER_SAVE 0x02 | HAPTIC_LPF 0x40)
        buffer[5] = m_rightRumble; // High-frequency weak motor
        buffer[6] = m_leftRumble;  // Low-frequency strong motor
        buffer[7] = m_headphoneVolume; // Headphone volume (100%)
        buffer[8] = m_speakerVolume;   // Controller internal speaker volume (100%)
        buffer[9] = 0x00; // Mic volume
        buffer[10] = m_audioRouting;   // Audio flags: route audio to internal speaker
        buffer[11] = static_cast<uint8_t>(m_muteLed);
        buffer[12] = 0x00; // Power save control

        // Right Trigger (R2) at byte 13
        buffer[13] = rightTriggerMode;
        memcpy(&buffer[14], rightTriggerParams, 10);

        // Left Trigger (L2) at byte 24
        buffer[24] = leftTriggerMode;
        memcpy(&buffer[25], leftTriggerParams, 10);

        // Audio preamp gain at byte 40
        buffer[40] = 0x02; // +6dB preamp gain

        // Valid flag 2 at byte 41
        buffer[41] = 0x07; // Valid flag 2: enable LED brightness (0x01) | lightbar setup (0x02) | compatible vibration v2 (0x04)

        // Lightbar setup & Player LEDs
        buffer[44] = 0x02; // 0x02 = DS_OUTPUT_LIGHTBAR_SETUP_LIGHT_OUT (Immediate host RGB)
        buffer[45] = 0x00; // Player LED brightness (0 = High)
        buffer[46] = static_cast<uint8_t>(m_playerLed) | 0x20; // 0x20 = instant player LED update bit
        buffer[47] = m_lightbarR; // Red
        buffer[48] = m_lightbarG; // Green
        buffer[49] = m_lightbarB; // Blue

        // Calculate Bluetooth CRC32 checksum over 0xA2 (BT HID output transaction byte) + buffer[0..73]
        uint8_t crcPayload[75] = {};
        crcPayload[0] = 0xA2;
        memcpy(&crcPayload[1], buffer, 74);
        uint32_t crc = CalculateCRC32(crcPayload, 75);

        buffer[74] = static_cast<uint8_t>(crc & 0xFF);
        buffer[75] = static_cast<uint8_t>((crc >> 8) & 0xFF);
        buffer[76] = static_cast<uint8_t>((crc >> 16) & 0xFF);
        buffer[77] = static_cast<uint8_t>((crc >> 24) & 0xFF);

        bytesToWrite = 78; // Report 0x31 is strictly 78 bytes!
    }

    OVERLAPPED writeOverlapped = {};
    writeOverlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    BOOL writeRes = WriteFile(m_deviceHandle, buffer, bytesToWrite, &bytesWritten, &writeOverlapped);
    if (!writeRes) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            DWORD waitRes = WaitForSingleObject(writeOverlapped.hEvent, 200);
            if (waitRes == WAIT_OBJECT_0) {
                GetOverlappedResult(m_deviceHandle, &writeOverlapped, &bytesWritten, FALSE);
            } else {
                CancelIoEx(m_deviceHandle, &writeOverlapped);
                DWORD dummy = 0;
                GetOverlappedResult(m_deviceHandle, &writeOverlapped, &dummy, TRUE);
            }
        } else {
            HidD_SetOutputReport(m_deviceHandle, buffer, bytesToWrite);
        }
    }

    if (writeOverlapped.hEvent) {
        CloseHandle(writeOverlapped.hEvent);
    }

    m_outputDirty = false;
}

void DualSenseController::Update(float dt) {
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_previousState = m_currentState;
        m_currentState = m_rawState;
    }

    // Handle temporary rumble timer
    if (m_rumbleTimer > 0.0f) {
        m_rumbleTimer -= dt;
        if (m_rumbleTimer <= 0.0f) {
            m_rumbleTimer = 0.0f;
            SetRumble(0.0f, 0.0f);
        }
    }

    // Handle temporary trigger vibration pulse timers
    if (m_leftTriggerPulseTimer > 0.0f) {
        m_leftTriggerPulseTimer -= dt;
        if (m_leftTriggerPulseTimer <= 0.0f) {
            m_leftTriggerPulseTimer = 0.0f;
            {
                std::lock_guard<std::mutex> lock(m_outputMutex);
                m_leftTriggerConfig = m_prePulseLeftTrigger;
                m_outputDirty = true;
            }
            SendOutputReport();
        }
    }
    if (m_rightTriggerPulseTimer > 0.0f) {
        m_rightTriggerPulseTimer -= dt;
        if (m_rightTriggerPulseTimer <= 0.0f) {
            m_rightTriggerPulseTimer = 0.0f;
            {
                std::lock_guard<std::mutex> lock(m_outputMutex);
                m_rightTriggerConfig = m_prePulseRightTrigger;
                m_outputDirty = true;
            }
            SendOutputReport();
        }
    }

    bool isConn = (m_currentState.connection != DualSenseConnection::Disconnected);

    // 1. Touchpad to Mouse Trackpad Emulation
    if (m_touchpadAsMouse && isConn) {
        const auto& t0 = m_currentState.touch[0];
        if (t0.active) {
            if (m_prevTouchActive) {
                float dx = (t0.x - m_prevTouchX) * 1920.0f;
                float dy = (t0.y - m_prevTouchY) * 1080.0f;
                if (std::abs(dx) < 180.0f && std::abs(dy) < 180.0f && (std::abs(dx) > 0.05f || std::abs(dy) > 0.05f)) {
                    mouse_event(MOUSEEVENTF_MOVE, static_cast<DWORD>(dx * m_touchpadSensitivity), static_cast<DWORD>(dy * m_touchpadSensitivity), 0, 0);
                }
            }
            m_prevTouchX = t0.x;
            m_prevTouchY = t0.y;
            m_prevTouchActive = true;
        } else {
            m_prevTouchActive = false;
        }

        bool padClick = IsButtonDown(DualSenseButton::Touchpad);
        if (padClick && !m_prevTouchpadClick) {
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
        } else if (!padClick && m_prevTouchpadClick) {
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        }
        m_prevTouchpadClick = padClick;
    }

    // 2. Audio-to-Haptics Engine (Dual-Band Bluetooth-aware)
    if (m_audioToHaptics && isConn && m_isActive) {
        auto& audioSys = EngineAudio::DualSenseAudioSystem::Get();

        // Bluetooth sync delay: defer haptic pulse by m_btAudioSyncDelay seconds
        // so the physical vibration in-hand matches the delayed Bluetooth audio
        if (m_btAudioSyncDelay > 0.001f) {
            m_btSyncDelayAccum += dt;
            if (m_btSyncDelayAccum < m_btAudioSyncDelay) {
                goto audio_haptics_done; // skip this frame
            }
            m_btSyncDelayAccum = 0.0f;
        }

        if (m_audioToHapticsDualBand) {
            // Dual-band mode: Bass -> left heavy motor, Treble -> right light motor
            float bassEnergy   = audioSys.GetBassAudioEnergy();
            float trebleEnergy = audioSys.GetTrebleAudioEnergy();
            float leftRumble  = std::clamp(bassEnergy   * m_audioToHapticsGain * m_bassHapticsGain,   0.0f, 1.0f);
            float rightRumble = std::clamp(trebleEnergy * m_audioToHapticsGain * m_trebleHapticsGain, 0.0f, 1.0f);
            if ((bassEnergy > 0.02f || trebleEnergy > 0.02f) && m_rumbleTimer <= 0.0f) {
                SetRumble(leftRumble, rightRumble);
            } else if (m_rumbleTimer <= 0.0f && (m_leftRumble > 0 || m_rightRumble > 0)) {
                SetRumble(0.0f, 0.0f);
            }
        } else {
            // Mono mode: classic single-energy both motors
            float energy = audioSys.GetAudioEnergy();
            if (energy > 0.03f) {
                float rumbleStrength = std::clamp(energy * m_audioToHapticsGain, 0.0f, 1.0f);
                if (m_rumbleTimer <= 0.0f) {
                    SetRumble(rumbleStrength * 0.85f, rumbleStrength * 0.55f);
                }
            } else if (m_rumbleTimer <= 0.0f && (m_leftRumble > 0 || m_rightRumble > 0)) {
                SetRumble(0.0f, 0.0f);
            }
        }
        audio_haptics_done:;
    }

    // 3. Dynamic Lightbar Animations
    m_animTimer += dt;
    if (isConn && m_isActive) {
        if (m_lightbarMode == 1) { // Rainbow Cycle
            float r = 0.5f + 0.5f * std::sin(m_animTimer * 1.8f);
            float g = 0.5f + 0.5f * std::sin(m_animTimer * 1.8f + 2.094f);
            float b = 0.5f + 0.5f * std::sin(m_animTimer * 1.8f + 4.188f);
            SetLightbarColor(r, g, b);
        } else if (m_lightbarMode == 2) { // Battery Status Gauge
            if (m_currentState.battery.level > 50) SetLightbar(0, 255, 60);
            else if (m_currentState.battery.level > 20) SetLightbar(255, 200, 0);
            else SetLightbar(255, 20, 20);
        } else if (m_lightbarMode == 3) { // Audio-Reactive Lightbar
            float energy = EngineAudio::DualSenseAudioSystem::Get().GetAudioEnergy();
            float factor = std::clamp(0.2f + energy * 2.5f, 0.0f, 1.0f);
            SetLightbarColor(m_activeProfile.lightbarColor[0] * factor,
                             m_activeProfile.lightbarColor[1] * factor,
                             m_activeProfile.lightbarColor[2] * factor);
        } else if (m_lightbarMode == 4) { // Breathing Glow
            float pulse = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(m_animTimer * 2.5f));
            SetLightbarColor(m_activeProfile.lightbarColor[0] * pulse,
                             m_activeProfile.lightbarColor[1] * pulse,
                             m_activeProfile.lightbarColor[2] * pulse);
        }
    }

    // Periodic keep-alive / output sync (at ~30-60 Hz)
    m_outputSendTimer += dt;
    if ((m_isActive || m_keepColorInBackground) && m_outputSendTimer >= 0.033f) {
        m_outputSendTimer = 0.0f;
        m_outputDirty = true;
    }
}

bool DualSenseController::IsButtonDown(DualSenseButton button) const {
    return (m_currentState.buttons & static_cast<uint32_t>(button)) != 0;
}

bool DualSenseController::IsButtonPressed(DualSenseButton button) const {
    uint32_t mask = static_cast<uint32_t>(button);
    return ((m_currentState.buttons & mask) != 0) && ((m_previousState.buttons & mask) == 0);
}

bool DualSenseController::IsButtonReleased(DualSenseButton button) const {
    uint32_t mask = static_cast<uint32_t>(button);
    return ((m_currentState.buttons & mask) == 0) && ((m_previousState.buttons & mask) != 0);
}

void DualSenseController::SetRumble(float leftMotor, float rightMotor) {
    if (!m_isActive) {
        if (m_leftRumble != 0 || m_rightRumble != 0) {
            m_leftRumble = 0;
            m_rightRumble = 0;
            m_outputDirty = true;
        }
        return;
    }
    uint8_t leftVal = static_cast<uint8_t>(std::clamp(leftMotor, 0.0f, 1.0f) * 255.0f);
    uint8_t rightVal = static_cast<uint8_t>(std::clamp(rightMotor, 0.0f, 1.0f) * 255.0f);

    if (m_leftRumble != leftVal || m_rightRumble != rightVal) {
        m_leftRumble = leftVal;
        m_rightRumble = rightVal;
        m_outputDirty = true;
    }
}

void DualSenseController::PlayRumble(float leftMotor, float rightMotor, float durationSeconds) {
    SetRumble(leftMotor, rightMotor);
    m_rumbleTimer = durationSeconds;
}

void DualSenseController::SetLightbar(uint8_t r, uint8_t g, uint8_t b) {
    std::lock_guard<std::mutex> lock(m_outputMutex);
    m_savedLightR = r;
    m_savedLightG = g;
    m_savedLightB = b;
    if (m_lightbarR != r || m_lightbarG != g || m_lightbarB != b) {
        m_lightbarR = r;
        m_lightbarG = g;
        m_lightbarB = b;
        m_outputDirty = true;
    }
}

void DualSenseController::SetLightbarColor(float r, float g, float b) {
    SetLightbar(
        static_cast<uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f),
        static_cast<uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f),
        static_cast<uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f)
    );
}

void DualSenseController::SetPlayerLed(DualSensePlayerLed led) {
    std::lock_guard<std::mutex> lock(m_outputMutex);
    m_savedPlayerLed = led;
    if (m_playerLed != led) {
        m_playerLed = led;
        m_outputDirty = true;
    }
}

void DualSenseController::SetMuteLed(DualSenseMuteLed led) {
    if (m_muteLed != led) {
        m_muteLed = led;
        m_outputDirty = true;
    }
}

void DualSenseController::SetSpeakerVolume(uint8_t volume) {
    if (m_speakerVolume != volume) {
        m_speakerVolume = volume;
        m_outputDirty = true;
    }
}

void DualSenseController::SetHeadphoneVolume(uint8_t volume) {
    if (m_headphoneVolume != volume) {
        m_headphoneVolume = volume;
        m_outputDirty = true;
    }
}

void DualSenseController::SetAudioRouting(uint8_t routingFlags) {
    if (m_audioRouting != routingFlags) {
        m_audioRouting = routingFlags;
        m_outputDirty = true;
    }
}

void DualSenseController::SetLeftTriggerEffect(const DualSenseTriggerConfig& config) {
    {
        std::lock_guard<std::mutex> lock(m_outputMutex);
        m_savedLeftTrigger = config;
        if (m_leftTriggerConfig == config) return;
        m_leftTriggerConfig = config;
        m_outputDirty = true;
    }
    if (m_isActive || m_keepColorInBackground) {
        SendOutputReport();
    }
}

void DualSenseController::SetRightTriggerEffect(const DualSenseTriggerConfig& config) {
    {
        std::lock_guard<std::mutex> lock(m_outputMutex);
        m_savedRightTrigger = config;
        if (m_rightTriggerConfig == config) return;
        m_rightTriggerConfig = config;
        m_outputDirty = true;
    }
    if (m_isActive || m_keepColorInBackground) {
        SendOutputReport();
    }
}

void DualSenseController::SetLeftTriggerResistance(uint8_t startPos, uint8_t force) {
    DualSenseTriggerConfig cfg;
    cfg.effect = DualSenseTriggerEffect::ContinuousResistance;
    cfg.startPosition = startPos;
    cfg.force = force;
    SetLeftTriggerEffect(cfg);
}

void DualSenseController::SetRightTriggerResistance(uint8_t startPos, uint8_t force) {
    DualSenseTriggerConfig cfg;
    cfg.effect = DualSenseTriggerEffect::ContinuousResistance;
    cfg.startPosition = startPos;
    cfg.force = force;
    SetRightTriggerEffect(cfg);
}

void DualSenseController::SetRightTriggerWeapon(uint8_t startPos, uint8_t endPos, uint8_t force, uint8_t frequency) {
    DualSenseTriggerConfig cfg;
    cfg.effect = DualSenseTriggerEffect::GallopingWeapon;
    cfg.startPosition = startPos;
    cfg.endPosition = endPos;
    cfg.force = force;
    cfg.frequency = frequency;
    SetRightTriggerEffect(cfg);
}

void DualSenseController::SetRightTriggerBow(uint8_t startPos, uint8_t endPos, uint8_t force, uint8_t snapForce) {
    DualSenseTriggerConfig cfg;
    cfg.effect = DualSenseTriggerEffect::Bow;
    cfg.startPosition = startPos;
    cfg.endPosition = endPos;
    cfg.force = force;
    cfg.extraForce = snapForce;
    SetRightTriggerEffect(cfg);
}

void DualSenseController::SetLeftTriggerVibration(uint8_t startPos, uint8_t amplitude, uint8_t frequency) {
    SetLeftTriggerVibration(startPos, 255, amplitude, frequency);
}

void DualSenseController::SetLeftTriggerVibration(uint8_t startPos, uint8_t endPos, uint8_t amplitude, uint8_t frequency) {
    DualSenseTriggerConfig cfg;
    cfg.effect = DualSenseTriggerEffect::Vibration;
    cfg.startPosition = startPos;
    cfg.endPosition = endPos;
    cfg.force = amplitude;
    cfg.frequency = frequency;
    SetLeftTriggerEffect(cfg);
}

void DualSenseController::SetRightTriggerVibration(uint8_t startPos, uint8_t amplitude, uint8_t frequency) {
    SetRightTriggerVibration(startPos, 255, amplitude, frequency);
}

void DualSenseController::SetRightTriggerVibration(uint8_t startPos, uint8_t endPos, uint8_t amplitude, uint8_t frequency) {
    DualSenseTriggerConfig cfg;
    cfg.effect = DualSenseTriggerEffect::Vibration;
    cfg.startPosition = startPos;
    cfg.endPosition = endPos;
    cfg.force = amplitude;
    cfg.frequency = frequency;
    SetRightTriggerEffect(cfg);
}

void DualSenseController::SetTriggersVibration(uint8_t startPos, uint8_t amplitude, uint8_t frequency) {
    SetTriggersVibration(startPos, 255, amplitude, frequency);
}

void DualSenseController::SetTriggersVibration(uint8_t startPos, uint8_t endPos, uint8_t amplitude, uint8_t frequency) {
    SetLeftTriggerVibration(startPos, endPos, amplitude, frequency);
    SetRightTriggerVibration(startPos, endPos, amplitude, frequency);
}

void DualSenseController::StopTriggerVibration(bool left, bool right) {
    DualSenseTriggerConfig empty;
    if (left && m_leftTriggerConfig.effect == DualSenseTriggerEffect::Vibration) {
        SetLeftTriggerEffect(empty);
    }
    if (right && m_rightTriggerConfig.effect == DualSenseTriggerEffect::Vibration) {
        SetRightTriggerEffect(empty);
    }
}

bool DualSenseController::IsTriggerVibrating(bool right) const {
    const auto& cfg = right ? m_rightTriggerConfig : m_leftTriggerConfig;
    return (cfg.effect == DualSenseTriggerEffect::Vibration && cfg.force > 0);
}

void DualSenseController::PlayTriggerVibration(bool left, bool right, uint8_t amplitude, uint8_t frequency, float durationSeconds) {
    {
        std::lock_guard<std::mutex> lock(m_outputMutex);
        DualSenseTriggerConfig pulseCfg;
        pulseCfg.effect = DualSenseTriggerEffect::Vibration;
        pulseCfg.startPosition = 0;
        pulseCfg.endPosition = 255;
        pulseCfg.force = amplitude;
        pulseCfg.frequency = frequency;

        if (left) {
            if (m_leftTriggerPulseTimer <= 0.0f) {
                m_prePulseLeftTrigger = m_leftTriggerConfig;
            }
            m_leftTriggerConfig = pulseCfg;
            m_leftTriggerPulseTimer = durationSeconds;
        }
        if (right) {
            if (m_rightTriggerPulseTimer <= 0.0f) {
                m_prePulseRightTrigger = m_rightTriggerConfig;
            }
            m_rightTriggerConfig = pulseCfg;
            m_rightTriggerPulseTimer = durationSeconds;
        }
        m_outputDirty = true;
    }
    if (m_isActive || m_keepColorInBackground) {
        SendOutputReport();
    }
}

void DualSenseController::ResetTriggers() {
    DualSenseTriggerConfig empty;
    SetLeftTriggerEffect(empty);
    SetRightTriggerEffect(empty);
}

void DualSenseController::SetDeadzone(float stickDeadzone, float triggerDeadzone) {
    m_stickDeadzone = std::clamp(stickDeadzone, 0.0f, 0.5f);
    m_triggerDeadzone = std::clamp(triggerDeadzone, 0.0f, 0.5f);
}

void DualSenseController::CalibrateGyro() {
    m_gyroBiasPitch = 0.0f;
    m_gyroBiasYaw = 0.0f;
    m_gyroBiasRoll = 0.0f;
    m_calibrationSamples = 0;
    m_calibrating = true;
}

void DualSenseController::ApplyDeadzone(float rawX, float rawY, float deadzone, float& outX, float& outY) {
    float magnitude = std::sqrt(rawX * rawX + rawY * rawY);
    if (magnitude <= deadzone) {
        outX = 0.0f;
        outY = 0.0f;
    } else {
        float normalizedMag = (std::min)(1.0f, (magnitude - deadzone) / (1.0f - deadzone));
        // Exponential response curve for precise fine adjustments and swift turns
        normalizedMag = normalizedMag * normalizedMag;
        outX = (rawX / magnitude) * normalizedMag;
        outY = (rawY / magnitude) * normalizedMag;
    }
}

DualSenseProfile DualSenseController::GetBuiltInProfile(DualSenseProfilePreset preset) {
    DualSenseProfile p;
    switch (preset) {
        case DualSenseProfilePreset::Shooter: {
            p.name = "Shooter / FPS";
            // L2: ADS Aim Wall
            p.leftTrigger.effect = DualSenseTriggerEffect::ContinuousResistance;
            p.leftTrigger.startPosition = 35;
            p.leftTrigger.force = 130;
            // R2: Machine Gun Recoil
            p.rightTrigger.effect = DualSenseTriggerEffect::GallopingWeapon;
            p.rightTrigger.startPosition = 35;
            p.rightTrigger.endPosition = 210;
            p.rightTrigger.force = 240;
            p.rightTrigger.frequency = 38;
            p.lightbarMode = 0; // Muzzle Flash Orange
            p.lightbarColor[0] = 1.0f; p.lightbarColor[1] = 0.25f; p.lightbarColor[2] = 0.0f;
            p.playerLed = DualSensePlayerLed::Player1;
            p.touchpadAsMouse = false;
            p.gyroAiming = true;
            p.gyroYawSens = 1.2f;
            p.gyroPitchSens = 1.0f;
            p.audioToHaptics = true;
            p.audioToHapticsGain = 1.3f;
            break;
        }
        case DualSenseProfilePreset::Racing: {
            p.name = "Racing / Simulator";
            // L2: ABS Brake Vibration
            p.leftTrigger.effect = DualSenseTriggerEffect::Vibration;
            p.leftTrigger.startPosition = 50;
            p.leftTrigger.force = 210;
            p.leftTrigger.frequency = 32;
            // R2: Throttle Resistance
            p.rightTrigger.effect = DualSenseTriggerEffect::ContinuousResistance;
            p.rightTrigger.startPosition = 20;
            p.rightTrigger.force = 160;
            p.lightbarMode = 0; // Green Tachometer
            p.lightbarColor[0] = 0.0f; p.lightbarColor[1] = 0.95f; p.lightbarColor[2] = 0.2f;
            p.playerLed = DualSensePlayerLed::Player1;
            p.audioToHaptics = true;
            p.audioToHapticsGain = 1.2f;
            break;
        }
        case DualSenseProfilePreset::Bow: {
            p.name = "Bow / Adventure";
            // L2: Zoom Aim Resistance
            p.leftTrigger.effect = DualSenseTriggerEffect::ContinuousResistance;
            p.leftTrigger.startPosition = 25;
            p.leftTrigger.force = 90;
            // R2: Authentic Bow Draw & Release
            p.rightTrigger.effect = DualSenseTriggerEffect::Bow;
            p.rightTrigger.startPosition = 30;
            p.rightTrigger.endPosition = 230;
            p.rightTrigger.force = 220;
            p.rightTrigger.extraForce = 60;
            p.lightbarMode = 0; // Forest Green
            p.lightbarColor[0] = 0.1f; p.lightbarColor[1] = 0.85f; p.lightbarColor[2] = 0.3f;
            p.playerLed = DualSensePlayerLed::Player1;
            p.audioToHaptics = true;
            p.audioToHapticsGain = 1.0f;
            break;
        }
        case DualSenseProfilePreset::Shotgun: {
            p.name = "Shotgun / Heavy";
            p.leftTrigger.effect = DualSenseTriggerEffect::ContinuousResistance;
            p.leftTrigger.startPosition = 40;
            p.leftTrigger.force = 110;
            // R2: Rigid Stop
            p.rightTrigger.effect = DualSenseTriggerEffect::RigidStop;
            p.rightTrigger.startPosition = 75;
            p.rightTrigger.force = 255;
            p.lightbarMode = 0; // Crimson Red
            p.lightbarColor[0] = 1.0f; p.lightbarColor[1] = 0.05f; p.lightbarColor[2] = 0.05f;
            p.playerLed = DualSensePlayerLed::Player1;
            p.audioToHaptics = true;
            p.audioToHapticsGain = 1.8f;
            break;
        }
        case DualSenseProfilePreset::OffRoad: {
            p.name = "Off-Road / Rally";
            p.leftTrigger.effect = DualSenseTriggerEffect::Vibration;
            p.leftTrigger.startPosition = 20;
            p.leftTrigger.force = 140;
            p.leftTrigger.frequency = 24;
            p.rightTrigger.effect = DualSenseTriggerEffect::ContinuousResistance;
            p.rightTrigger.startPosition = 30;
            p.rightTrigger.force = 130;
            p.lightbarMode = 0; // Amber
            p.lightbarColor[0] = 0.95f; p.lightbarColor[1] = 0.55f; p.lightbarColor[2] = 0.1f;
            p.playerLed = DualSensePlayerLed::Player1;
            p.audioToHaptics = true;
            p.audioToHapticsGain = 1.4f;
            break;
        }
        case DualSenseProfilePreset::Default:
        default: {
            p.name = "Default Profile";
            p.leftTrigger.effect = DualSenseTriggerEffect::None;
            p.rightTrigger.effect = DualSenseTriggerEffect::None;
            p.lightbarMode = 0; // PlayStation Blue
            p.lightbarColor[0] = 0.0f; p.lightbarColor[1] = 0.47f; p.lightbarColor[2] = 1.0f;
            p.playerLed = DualSensePlayerLed::Player1;
            p.muteLed = DualSenseMuteLed::Off;
            p.touchpadAsMouse = false;
            p.gyroAiming = false;
            p.audioToHaptics = true;
            p.audioToHapticsGain = 1.0f;
            break;
        }
    }
    return p;
}

void DualSenseController::ApplyProfile(const DualSenseProfile& profile) {
    m_activeProfile = profile;
    SetLeftTriggerEffect(profile.leftTrigger);
    SetRightTriggerEffect(profile.rightTrigger);
    SetLightbarMode(profile.lightbarMode);
    SetLightbarColor(profile.lightbarColor[0], profile.lightbarColor[1], profile.lightbarColor[2]);
    SetPlayerLed(profile.playerLed);
    SetMuteLed(profile.muteLed);
    SetTouchpadAsMouse(profile.touchpadAsMouse);
    SetTouchpadSensitivity(profile.touchpadSensitivity);
    SetGyroAimingEnabled(profile.gyroAiming);
    SetGyroSensitivity(profile.gyroYawSens, profile.gyroPitchSens);
    SetAudioToHaptics(profile.audioToHaptics);
    SetAudioToHapticsGain(profile.audioToHapticsGain);
    SetDeadzone(profile.stickDeadzone, profile.triggerDeadzone);
}

bool DualSenseController::SaveProfile(const std::string& name) {
    _mkdir("Config");
    _mkdir("Config/DualSenseProfiles");
    std::string safeName = name.empty() ? "CustomProfile" : name;
    for (char& c : safeName) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '\"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    std::string path = "Config/DualSenseProfiles/" + safeName + ".ds_profile";
    std::ofstream out(path);
    if (!out.is_open()) return false;

    out << "# DualSense Profile Configuration\n";
    out << "Name=" << safeName << "\n";
    out << "L2Effect=" << static_cast<int>(m_activeProfile.leftTrigger.effect) << "\n";
    out << "L2Start=" << (int)m_activeProfile.leftTrigger.startPosition << "\n";
    out << "L2End=" << (int)m_activeProfile.leftTrigger.endPosition << "\n";
    out << "L2Force=" << (int)m_activeProfile.leftTrigger.force << "\n";
    out << "L2Freq=" << (int)m_activeProfile.leftTrigger.frequency << "\n";
    out << "L2Extra=" << (int)m_activeProfile.leftTrigger.extraForce << "\n";
    out << "R2Effect=" << static_cast<int>(m_activeProfile.rightTrigger.effect) << "\n";
    out << "R2Start=" << (int)m_activeProfile.rightTrigger.startPosition << "\n";
    out << "R2End=" << (int)m_activeProfile.rightTrigger.endPosition << "\n";
    out << "R2Force=" << (int)m_activeProfile.rightTrigger.force << "\n";
    out << "R2Freq=" << (int)m_activeProfile.rightTrigger.frequency << "\n";
    out << "R2Extra=" << (int)m_activeProfile.rightTrigger.extraForce << "\n";
    out << "LightMode=" << m_activeProfile.lightbarMode << "\n";
    out << "LightColorR=" << m_activeProfile.lightbarColor[0] << "\n";
    out << "LightColorG=" << m_activeProfile.lightbarColor[1] << "\n";
    out << "LightColorB=" << m_activeProfile.lightbarColor[2] << "\n";
    out << "PlayerLed=" << static_cast<int>(m_activeProfile.playerLed) << "\n";
    out << "MuteLed=" << static_cast<int>(m_activeProfile.muteLed) << "\n";
    out << "TouchAsMouse=" << (m_activeProfile.touchpadAsMouse ? 1 : 0) << "\n";
    out << "TouchSens=" << m_activeProfile.touchpadSensitivity << "\n";
    out << "GyroAim=" << (m_activeProfile.gyroAiming ? 1 : 0) << "\n";
    out << "GyroYawSens=" << m_activeProfile.gyroYawSens << "\n";
    out << "GyroPitchSens=" << m_activeProfile.gyroPitchSens << "\n";
    out << "AudioHaptics=" << (m_activeProfile.audioToHaptics ? 1 : 0) << "\n";
    out << "AudioHapticsGain=" << m_activeProfile.audioToHapticsGain << "\n";
    out << "StickDeadzone=" << m_activeProfile.stickDeadzone << "\n";
    out << "TrigDeadzone=" << m_activeProfile.triggerDeadzone << "\n";

    return true;
}

bool DualSenseController::LoadProfile(const std::string& name) {
    std::string safeName = name;
    for (char& c : safeName) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '\"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    std::string path = "Config/DualSenseProfiles/" + safeName + ".ds_profile";
    std::ifstream in(path);
    if (!in.is_open()) return false;

    DualSenseProfile p;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;
        std::string key = line.substr(0, eqPos);
        std::string val = line.substr(eqPos + 1);

        if (key == "Name") p.name = val;
        else if (key == "L2Effect") p.leftTrigger.effect = static_cast<DualSenseTriggerEffect>(std::atoi(val.c_str()));
        else if (key == "L2Start") p.leftTrigger.startPosition = static_cast<uint8_t>(std::atoi(val.c_str()));
        else if (key == "L2End") p.leftTrigger.endPosition = static_cast<uint8_t>(std::atoi(val.c_str()));
        else if (key == "L2Force") p.leftTrigger.force = static_cast<uint8_t>(std::atoi(val.c_str()));
        else if (key == "L2Freq") p.leftTrigger.frequency = static_cast<uint8_t>(std::atoi(val.c_str()));
        else if (key == "L2Extra") p.leftTrigger.extraForce = static_cast<uint8_t>(std::atoi(val.c_str()));
        else if (key == "R2Effect") p.rightTrigger.effect = static_cast<DualSenseTriggerEffect>(std::atoi(val.c_str()));
        else if (key == "R2Start") p.rightTrigger.startPosition = static_cast<uint8_t>(std::atoi(val.c_str()));
        else if (key == "R2End") p.rightTrigger.endPosition = static_cast<uint8_t>(std::atoi(val.c_str()));
        else if (key == "R2Force") p.rightTrigger.force = static_cast<uint8_t>(std::atoi(val.c_str()));
        else if (key == "R2Freq") p.rightTrigger.frequency = static_cast<uint8_t>(std::atoi(val.c_str()));
        else if (key == "R2Extra") p.rightTrigger.extraForce = static_cast<uint8_t>(std::atoi(val.c_str()));
        else if (key == "LightMode") p.lightbarMode = std::atoi(val.c_str());
        else if (key == "LightColorR") p.lightbarColor[0] = static_cast<float>(std::atof(val.c_str()));
        else if (key == "LightColorG") p.lightbarColor[1] = static_cast<float>(std::atof(val.c_str()));
        else if (key == "LightColorB") p.lightbarColor[2] = static_cast<float>(std::atof(val.c_str()));
        else if (key == "PlayerLed") p.playerLed = static_cast<DualSensePlayerLed>(std::atoi(val.c_str()));
        else if (key == "MuteLed") p.muteLed = static_cast<DualSenseMuteLed>(std::atoi(val.c_str()));
        else if (key == "TouchAsMouse") p.touchpadAsMouse = (std::atoi(val.c_str()) != 0);
        else if (key == "TouchSens") p.touchpadSensitivity = static_cast<float>(std::atof(val.c_str()));
        else if (key == "GyroAim") p.gyroAiming = (std::atoi(val.c_str()) != 0);
        else if (key == "GyroYawSens") p.gyroYawSens = static_cast<float>(std::atof(val.c_str()));
        else if (key == "GyroPitchSens") p.gyroPitchSens = static_cast<float>(std::atof(val.c_str()));
        else if (key == "AudioHaptics") p.audioToHaptics = (std::atoi(val.c_str()) != 0);
        else if (key == "AudioHapticsGain") p.audioToHapticsGain = static_cast<float>(std::atof(val.c_str()));
        else if (key == "StickDeadzone") p.stickDeadzone = static_cast<float>(std::atof(val.c_str()));
    }

    ApplyProfile(p);
    return true;
}

std::vector<std::string> DualSenseController::GetAvailableProfiles() const {
    std::vector<std::string> results;
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA("Config/DualSenseProfiles/*.ds_profile", &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::string fname = fd.cFileName;
                if (fname.size() > 11 && fname.substr(fname.size() - 11) == ".ds_profile") {
                    results.push_back(fname.substr(0, fname.size() - 11));
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    return results;
}

} // namespace EngineInput

#elif defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)

#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/Sysmodule.h>
#include <iostream>
#include <cmath>

namespace EngineInput {

static int s_ps4PadHandle = -1;
static int s_ps4UserId = -1;

DualSenseController& DualSenseController::Get() {
    static DualSenseController s_instance;
    return s_instance;
}

DualSenseController::DualSenseController() = default;
DualSenseController::~DualSenseController() {
    Shutdown();
}

bool DualSenseController::Initialize() {
    if (s_ps4PadHandle >= 0) return true;

    scePadInit();

    OrbisUserServiceInitializeParams param = {};
    param.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    sceUserServiceInitialize(&param);
    s_ps4UserId = -1;

    // 1. Try initial user
    if (sceUserServiceGetInitialUser(&s_ps4UserId) == 0 && s_ps4UserId > 0) {
        s_ps4PadHandle = scePadOpen(s_ps4UserId, ORBIS_PAD_PORT_TYPE_STANDARD, 0, nullptr);
    }
    // 2. Try foreground active user
    if (s_ps4PadHandle < 0) {
        int32_t fgUser = -1;
        if (sceUserServiceGetForegroundUser(&fgUser) == 0 && fgUser > 0) {
            s_ps4PadHandle = scePadOpen(fgUser, ORBIS_PAD_PORT_TYPE_STANDARD, 0, nullptr);
            if (s_ps4PadHandle >= 0) s_ps4UserId = fgUser;
        }
    }
    // 3. Try any logged-in user
    if (s_ps4PadHandle < 0) {
        OrbisUserServiceLoginUserIdList list = {};
        if (sceUserServiceGetLoginUserIdList(&list) == 0) {
            for (int i = 0; i < 4; ++i) {
                if (list.userId[i] > 0) {
                    s_ps4PadHandle = scePadOpen(list.userId[i], ORBIS_PAD_PORT_TYPE_STANDARD, 0, nullptr);
                    if (s_ps4PadHandle >= 0) {
                        s_ps4UserId = list.userId[i];
                        break;
                    }
                }
            }
        }
    }
    // 4. Fallback default user ID 1000 or 0
    if (s_ps4PadHandle < 0) {
        s_ps4PadHandle = scePadOpen(1000, ORBIS_PAD_PORT_TYPE_STANDARD, 0, nullptr);
    }
    if (s_ps4PadHandle < 0) {
        s_ps4PadHandle = scePadOpen(0, ORBIS_PAD_PORT_TYPE_STANDARD, 0, nullptr);
    }

    if (s_ps4PadHandle >= 0) {
        m_currentState.connection = DualSenseConnection::USB;
        m_physicalConnection = DualSenseConnection::USB;
        std::cout << "[DualSense PS4] Opened pad handle " << s_ps4PadHandle << " for user " << s_ps4UserId << std::endl;
        return true;
    }

    std::cerr << "[DualSense PS4] Failed to open controller handle" << std::endl;
    return false;
}

int GetPS4PadHandle() {
    if (s_ps4PadHandle < 0) {
        DualSenseController::Get().Initialize();
    }
    return s_ps4PadHandle;
}

void DualSenseController::Shutdown() {
    if (s_ps4PadHandle >= 0) {
        scePadClose(s_ps4PadHandle);
        s_ps4PadHandle = -1;
    }
}

void DualSenseController::Update(float dt) {
    m_previousState = m_currentState;

    if (s_ps4PadHandle < 0) {
        Initialize();
        if (s_ps4PadHandle < 0) return;
    }

    OrbisPadData padData = {};
    int rc = scePadReadState(s_ps4PadHandle, &padData);
    if (rc != 0) {
        m_currentState.connection = DualSenseConnection::Disconnected;
        return;
    }

    // Verify valid controller input (connected flag or non-zero stick / button data)
    bool isConnected = (padData.connected != 0) ||
                       (padData.leftStick.x != 0 || padData.leftStick.y != 0 ||
                        padData.rightStick.x != 0 || padData.rightStick.y != 0 ||
                        padData.buttons != 0);
    if (!isConnected) {
        m_currentState.connection = DualSenseConnection::Disconnected;
        m_currentState.leftStickX = 0.0f;
        m_currentState.leftStickY = 0.0f;
        m_currentState.rightStickX = 0.0f;
        m_currentState.rightStickY = 0.0f;
        return;
    }

    m_currentState.connection = DualSenseConnection::USB;

    // 1. Button mapping
    uint32_t buttons = 0;
    if (padData.buttons & ORBIS_PAD_BUTTON_CROSS)      buttons |= DualSenseButton::Cross;
    if (padData.buttons & ORBIS_PAD_BUTTON_CIRCLE)     buttons |= DualSenseButton::Circle;
    if (padData.buttons & ORBIS_PAD_BUTTON_SQUARE)     buttons |= DualSenseButton::Square;
    if (padData.buttons & ORBIS_PAD_BUTTON_TRIANGLE)   buttons |= DualSenseButton::Triangle;
    if (padData.buttons & ORBIS_PAD_BUTTON_UP)         buttons |= DualSenseButton::DPadUp;
    if (padData.buttons & ORBIS_PAD_BUTTON_RIGHT)      buttons |= DualSenseButton::DPadRight;
    if (padData.buttons & ORBIS_PAD_BUTTON_DOWN)       buttons |= DualSenseButton::DPadDown;
    if (padData.buttons & ORBIS_PAD_BUTTON_LEFT)       buttons |= DualSenseButton::DPadLeft;
    if (padData.buttons & ORBIS_PAD_BUTTON_L1)         buttons |= DualSenseButton::L1;
    if (padData.buttons & ORBIS_PAD_BUTTON_R1)         buttons |= DualSenseButton::R1;
    if (padData.buttons & ORBIS_PAD_BUTTON_L2)         buttons |= DualSenseButton::L2Button;
    if (padData.buttons & ORBIS_PAD_BUTTON_R2)         buttons |= DualSenseButton::R2Button;
    if (padData.buttons & ORBIS_PAD_BUTTON_L3)         buttons |= DualSenseButton::L3;
    if (padData.buttons & ORBIS_PAD_BUTTON_R3)         buttons |= DualSenseButton::R3;
    if (padData.buttons & ORBIS_PAD_BUTTON_OPTIONS)    buttons |= DualSenseButton::Options;
    if (padData.buttons & ORBIS_PAD_BUTTON_TOUCH_PAD)  buttons |= DualSenseButton::Touchpad;

    m_currentState.buttons = static_cast<DualSenseButton>(buttons);

    // 2. Analog sticks [-1.0f .. 1.0f]
    m_currentState.leftStickX  = (static_cast<float>(padData.leftStick.x) - 128.0f) / 128.0f;
    m_currentState.leftStickY  = -(static_cast<float>(padData.leftStick.y) - 128.0f) / 128.0f;
    m_currentState.rightStickX = (static_cast<float>(padData.rightStick.x) - 128.0f) / 128.0f;
    m_currentState.rightStickY = -(static_cast<float>(padData.rightStick.y) - 128.0f) / 128.0f;

    // Deadzone filter
    if (std::abs(m_currentState.leftStickX) < m_stickDeadzone) m_currentState.leftStickX = 0.0f;
    if (std::abs(m_currentState.leftStickY) < m_stickDeadzone) m_currentState.leftStickY = 0.0f;
    if (std::abs(m_currentState.rightStickX) < m_stickDeadzone) m_currentState.rightStickX = 0.0f;
    if (std::abs(m_currentState.rightStickY) < m_stickDeadzone) m_currentState.rightStickY = 0.0f;

    // 3. Analog Triggers [0.0f .. 1.0f]
    m_currentState.leftTrigger  = static_cast<float>(padData.analogButtons.l2) / 255.0f;
    m_currentState.rightTrigger = static_cast<float>(padData.analogButtons.r2) / 255.0f;

    // 4. Motion Sensors
    m_currentState.motion.gyroPitch = padData.angularVelocity.x;
    m_currentState.motion.gyroYaw   = padData.angularVelocity.y;
    m_currentState.motion.gyroRoll  = padData.angularVelocity.z;
    m_currentState.motion.accelX    = padData.acceleration.x;
    m_currentState.motion.accelY    = padData.acceleration.y;
    m_currentState.motion.accelZ    = padData.acceleration.z;

    // 5. Touchpad
    if (padData.touch.fingers > 0) {
        m_currentState.touch[0].active = true;
        m_currentState.touch[0].x = static_cast<float>(padData.touch.touch[0].x) / 1920.0f;
        m_currentState.touch[0].y = static_cast<float>(padData.touch.touch[0].y) / 940.0f;
    } else {
        m_currentState.touch[0].active = false;
    }

    // 6. Handle rumble duration timer
    if (m_rumbleTimer > 0.0f) {
        m_rumbleTimer -= dt;
        if (m_rumbleTimer <= 0.0f) {
            SetRumble(0.0f, 0.0f);
        }
    }
}

bool DualSenseController::IsButtonDown(DualSenseButton button) const {
    return (m_currentState.buttons & button) != DualSenseButton::None;
}

bool DualSenseController::IsButtonPressed(DualSenseButton button) const {
    return ((m_currentState.buttons & button) != DualSenseButton::None) &&
           ((m_previousState.buttons & button) == DualSenseButton::None);
}

bool DualSenseController::IsButtonReleased(DualSenseButton button) const {
    return ((m_currentState.buttons & button) == DualSenseButton::None) &&
           ((m_previousState.buttons & button) != DualSenseButton::None);
}

void DualSenseController::SetRumble(float leftMotor, float rightMotor) {
    if (s_ps4PadHandle < 0) return;

    OrbisPadVibeParam vibe = {};
    vibe.lgMotor = static_cast<uint8_t>(std::clamp(leftMotor, 0.0f, 1.0f) * 255.0f);
    vibe.smMotor = static_cast<uint8_t>(std::clamp(rightMotor, 0.0f, 1.0f) * 255.0f);
    scePadSetVibration(s_ps4PadHandle, &vibe);
}

void DualSenseController::PlayRumble(float leftMotor, float rightMotor, float durationSeconds) {
    m_rumbleTimer = durationSeconds;
    SetRumble(leftMotor, rightMotor);
}

void DualSenseController::SetLightbar(uint8_t r, uint8_t g, uint8_t b) {
    m_lightbarR = r;
    m_lightbarG = g;
    m_lightbarB = b;

    if (s_ps4PadHandle < 0) return;

    OrbisPadColor color = {};
    color.r = r;
    color.g = g;
    color.b = b;
    color.a = 255;
    scePadSetLightBar(s_ps4PadHandle, &color);
}

void DualSenseController::SetLightbarColor(float r, float g, float b) {
    SetLightbar(static_cast<uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f),
                static_cast<uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f),
                static_cast<uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f));
}

void DualSenseController::SetPlayerLed(DualSensePlayerLed led) { m_playerLed = led; }
void DualSenseController::SetMuteLed(DualSenseMuteLed led) { m_muteLed = led; }
void DualSenseController::SetSpeakerVolume(uint8_t volume) { m_speakerVolume = volume; }
void DualSenseController::SetHeadphoneVolume(uint8_t volume) { m_headphoneVolume = volume; }
void DualSenseController::SetAudioRouting(uint8_t routing) { m_audioRouting = routing; }

void DualSenseController::Activate() {}
void DualSenseController::Pause() {}
void DualSenseController::Resume() {}
void DualSenseController::TurnOffAll() { SetRumble(0.0f, 0.0f); }

void DualSenseController::SetLeftTriggerEffect(const DualSenseTriggerConfig&) {}
void DualSenseController::SetRightTriggerEffect(const DualSenseTriggerConfig&) {}
void DualSenseController::SetLeftTriggerResistance(uint8_t, uint8_t) {}
void DualSenseController::SetRightTriggerResistance(uint8_t, uint8_t) {}
void DualSenseController::SetRightTriggerWeapon(uint8_t, uint8_t, uint8_t, uint8_t) {}
void DualSenseController::SetRightTriggerBow(uint8_t, uint8_t, uint8_t, uint8_t) {}
void DualSenseController::SetLeftTriggerVibration(uint8_t, uint8_t, uint8_t) {}
void DualSenseController::SetLeftTriggerVibration(uint8_t, uint8_t, uint8_t, uint8_t) {}
void DualSenseController::SetRightTriggerVibration(uint8_t, uint8_t, uint8_t) {}
void DualSenseController::SetRightTriggerVibration(uint8_t, uint8_t, uint8_t, uint8_t) {}
void DualSenseController::SetTriggersVibration(uint8_t, uint8_t, uint8_t) {}
void DualSenseController::SetTriggersVibration(uint8_t, uint8_t, uint8_t, uint8_t) {}
void DualSenseController::PlayTriggerVibration(bool, bool, uint8_t, uint8_t, float) {}
void DualSenseController::StopTriggerVibration(bool, bool) {}
bool DualSenseController::IsTriggerVibrating(bool) const { return false; }
void DualSenseController::ResetTriggers() {}

void DualSenseController::SetDeadzone(float stickDeadzone, float triggerDeadzone) {
    m_stickDeadzone = stickDeadzone;
    m_triggerDeadzone = triggerDeadzone;
}

void DualSenseController::CalibrateGyro() {
    if (s_ps4PadHandle >= 0) {
        scePadResetOrientation(s_ps4PadHandle);
    }
}

DualSenseProfile DualSenseController::GetBuiltInProfile(DualSenseProfilePreset) { return {}; }
void DualSenseController::ApplyProfile(const DualSenseProfile&) {}
bool DualSenseController::SaveProfile(const std::string&) { return true; }
bool DualSenseController::LoadProfile(const std::string&) { return true; }
std::vector<std::string> DualSenseController::GetAvailableProfiles() const { return {}; }

} // namespace EngineInput

#else

namespace EngineInput {

DualSenseController& DualSenseController::Get() {
    static DualSenseController s_instance;
    return s_instance;
}

DualSenseController::DualSenseController() = default;
DualSenseController::~DualSenseController() = default;

bool DualSenseController::Initialize() { return true; }
void DualSenseController::Shutdown() {}
void DualSenseController::Update(float) {}

bool DualSenseController::IsButtonDown(DualSenseButton) const { return false; }
bool DualSenseController::IsButtonPressed(DualSenseButton) const { return false; }
bool DualSenseController::IsButtonReleased(DualSenseButton) const { return false; }

void DualSenseController::SetRumble(float, float) {}
void DualSenseController::PlayRumble(float, float, float) {}
void DualSenseController::SetLightbar(uint8_t, uint8_t, uint8_t) {}
void DualSenseController::SetLightbarColor(float, float, float) {}
void DualSenseController::SetPlayerLed(DualSensePlayerLed) {}
void DualSenseController::SetMuteLed(DualSenseMuteLed) {}
void DualSenseController::SetSpeakerVolume(uint8_t) {}
void DualSenseController::SetHeadphoneVolume(uint8_t) {}
void DualSenseController::SetAudioRouting(uint8_t) {}

void DualSenseController::Activate() {}
void DualSenseController::Pause() {}
void DualSenseController::Resume() {}
void DualSenseController::TurnOffAll() {}

void DualSenseController::SetLeftTriggerEffect(const DualSenseTriggerConfig&) {}
void DualSenseController::SetRightTriggerEffect(const DualSenseTriggerConfig&) {}
void DualSenseController::SetLeftTriggerResistance(uint8_t, uint8_t) {}
void DualSenseController::SetRightTriggerResistance(uint8_t, uint8_t) {}
void DualSenseController::SetRightTriggerWeapon(uint8_t, uint8_t, uint8_t, uint8_t) {}
void DualSenseController::SetRightTriggerBow(uint8_t, uint8_t, uint8_t, uint8_t) {}
void DualSenseController::SetLeftTriggerVibration(uint8_t, uint8_t, uint8_t) {}
void DualSenseController::SetLeftTriggerVibration(uint8_t, uint8_t, uint8_t, uint8_t) {}
void DualSenseController::SetRightTriggerVibration(uint8_t, uint8_t, uint8_t) {}
void DualSenseController::SetRightTriggerVibration(uint8_t, uint8_t, uint8_t, uint8_t) {}
void DualSenseController::SetTriggersVibration(uint8_t, uint8_t, uint8_t) {}
void DualSenseController::SetTriggersVibration(uint8_t, uint8_t, uint8_t, uint8_t) {}
void DualSenseController::PlayTriggerVibration(bool, bool, uint8_t, uint8_t, float) {}
void DualSenseController::StopTriggerVibration(bool, bool) {}
bool DualSenseController::IsTriggerVibrating(bool) const { return false; }
void DualSenseController::ResetTriggers() {}

void DualSenseController::SetDeadzone(float, float) {}
void DualSenseController::CalibrateGyro() {}

DualSenseProfile DualSenseController::GetBuiltInProfile(DualSenseProfilePreset) { return {}; }
void DualSenseController::ApplyProfile(const DualSenseProfile&) {}
bool DualSenseController::SaveProfile(const std::string&) { return true; }
bool DualSenseController::LoadProfile(const std::string&) { return true; }
std::vector<std::string> DualSenseController::GetAvailableProfiles() const { return {}; }

} // namespace EngineInput

#endif
