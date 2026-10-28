#include "DualShock.h"

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

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "xinput.lib")

static const GUID GUID_DEVINTERFACE_HID_CLASS = { 0x4D1E55B2L, 0xF16F, 0x11CF, { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

#elif defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
#include "Platform/PlatformTypes.h"
#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <cmath>
#include <algorithm>
#include <iostream>
#endif

namespace EngineInput {

// Sony Corporation USB Vendor ID
static constexpr USHORT SONY_VENDOR_ID = 0x054C;
// DualShock 4 Gen 1 (CUH-ZCT1x) Product ID
static constexpr USHORT DUALSHOCK4_GEN1_PRODUCT_ID = 0x05C4;
// DualShock 4 Gen 2 (CUH-ZCT2x) Product ID
static constexpr USHORT DUALSHOCK4_GEN2_PRODUCT_ID = 0x09CC;
// DualShock 4 Wireless USB Adaptor / Dongle Product ID
static constexpr USHORT DUALSHOCK4_DONGLE_PRODUCT_ID = 0x0BA0;

static uint32_t s_ds4CrcTable[256];
static bool s_ds4CrcTableInitialized = false;

static void InitializeDS4CRCTable() {
    if (s_ds4CrcTableInitialized) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        s_ds4CrcTable[i] = crc;
    }
    s_ds4CrcTableInitialized = true;
}

uint32_t DualShockController::CalculateCRC32(const uint8_t* data, size_t size) {
    InitializeDS4CRCTable();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; ++i) {
        crc = (crc >> 8) ^ s_ds4CrcTable[(crc ^ data[i]) & 0xFF];
    }
    return ~crc;
}

DualShockController& DualShockController::Get() {
    static DualShockController s_instance;
    return s_instance;
}

DualShockController::DualShockController() {
    InitializeDS4CRCTable();
    m_lightbarR = 0;
    m_lightbarG = 64;
    m_lightbarB = 255;
    m_outputDirty = true;
}

DualShockController::~DualShockController() {
    Shutdown();
}

#if defined(_WIN32)

bool DualShockController::Initialize() {
    if (m_running.load()) return true;

    m_running.store(true);
    m_workerThread = std::thread(&DualShockController::DeviceWorkerThread, this);
    std::cout << "[DualShock 4] Worker thread launched" << std::endl;
    return true;
}

void DualShockController::Shutdown() {
    if (!m_running.load()) return;

    m_running.store(false);
    TurnOffAll();

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    CloseDevice();
}

void DualShockController::CloseDevice() {
    if (m_deviceHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_deviceHandle);
        m_deviceHandle = INVALID_HANDLE_VALUE;
    }
    m_connectionType = DualShockConnection::Disconnected;
    m_physicalConnection = DualShockConnection::Disconnected;
    m_devicePath.clear();

    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_rawState = {};
    m_currentState = {};
}

bool DualShockController::EnumerateAndOpenDevice() {
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_HID_CLASS,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );

    if (deviceInfoSet == INVALID_HANDLE_VALUE) return false;

    SP_DEVICE_INTERFACE_DATA interfaceData = {};
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    std::vector<std::wstring> candidatePaths;
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfoSet, nullptr, &GUID_DEVINTERFACE_HID_CLASS, i, &interfaceData); ++i) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &interfaceData, nullptr, 0, &requiredSize, nullptr);
        if (requiredSize == 0) continue;

        std::vector<BYTE> detailBuffer(requiredSize);
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuffer.data());
        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &interfaceData, detailData, requiredSize, nullptr, nullptr)) {
            candidatePaths.emplace_back(detailData->DevicePath);
        }
    }
    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    bool found = false;
    for (const auto& devPath : candidatePaths) {
        std::wstring pathUpper = devPath;
        for (auto& c : pathUpper) c = towupper(c);

        bool pathHasSonyVid = (pathUpper.find(L"VID_054C") != std::wstring::npos || pathUpper.find(L"VID&0002054C") != std::wstring::npos);
        bool pathHasDualShock4Pid = (pathUpper.find(L"PID_05C4") != std::wstring::npos || pathUpper.find(L"PID&05C4") != std::wstring::npos ||
                                     pathUpper.find(L"PID_09CC") != std::wstring::npos || pathUpper.find(L"PID&09CC") != std::wstring::npos ||
                                     pathUpper.find(L"PID_0BA0") != std::wstring::npos || pathUpper.find(L"PID&0BA0") != std::wstring::npos);

        if (!pathHasSonyVid && !pathHasDualShock4Pid) continue;

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

        if (handle == INVALID_HANDLE_VALUE) continue;

        bool isDS4 = false;
        HIDD_ATTRIBUTES attributes = {};
        attributes.Size = sizeof(HIDD_ATTRIBUTES);
        if (HidD_GetAttributes(handle, &attributes)) {
            if (attributes.VendorID == SONY_VENDOR_ID) {
                if (attributes.ProductID == DUALSHOCK4_GEN1_PRODUCT_ID ||
                    attributes.ProductID == DUALSHOCK4_GEN2_PRODUCT_ID ||
                    attributes.ProductID == DUALSHOCK4_DONGLE_PRODUCT_ID) {
                    isDS4 = true;
                }
            }
        }

        if (!isDS4 && pathHasSonyVid && pathHasDualShock4Pid) {
            isDS4 = true;
        }

        if (!isDS4) {
            CloseHandle(handle);
            continue;
        }

        PHIDP_PREPARSED_DATA preparsedData = nullptr;
        HIDP_CAPS caps = {};
        bool isGamepadInterface = true;

        if (HidD_GetPreparsedData(handle, &preparsedData)) {
            if (HidP_GetCaps(preparsedData, &caps) == HIDP_STATUS_SUCCESS) {
                m_inputReportLength = caps.InputReportByteLength;
                m_outputReportLength = caps.OutputReportByteLength;
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

        int strLen = WideCharToMultiByte(CP_UTF8, 0, devPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (strLen > 0) {
            m_devicePath.resize(strLen - 1);
            WideCharToMultiByte(CP_UTF8, 0, devPath.c_str(), -1, &m_devicePath[0], strLen, nullptr, nullptr);
        }

        wchar_t prodStr[128] = {};
        if (HidD_GetProductString(handle, prodStr, sizeof(prodStr))) {
            m_productString = prodStr;
        } else {
            m_productString = L"DualShock 4 Wireless Controller";
        }

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

        if (pathIsBluetooth || (!pathIsUsb && reportLenIsBt)) {
            m_physicalConnection = DualShockConnection::Bluetooth;
        } else {
            m_physicalConnection = DualShockConnection::USB;
        }
        m_connectionType = m_physicalConnection;

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_rawState.connection = m_physicalConnection;
            m_currentState.connection = m_physicalConnection;
        }

        m_outputDirty = true;
        found = true;
        std::wcout << L"[DualShock 4] Connected: " << m_productString << L" via "
                   << (m_physicalConnection == DualShockConnection::Bluetooth ? L"Bluetooth" : L"USB") << std::endl;
        break;
    }

    return found;
}

void DualShockController::DeviceWorkerThread() {
    uint8_t inputBuffer[512] = {};
    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    while (m_running.load()) {
        if (m_deviceHandle == INVALID_HANDLE_VALUE) {
            if (!EnumerateAndOpenDevice()) {
                // Controller not found, sleep 100ms and retry
                for (int i = 0; i < 2 && m_running.load(); ++i) {
                    Sleep(50);
                }
                continue;
            }
            m_outputDirty = true;
        }

        DWORD bytesToRead = (std::min)(static_cast<DWORD>(sizeof(inputBuffer)), static_cast<DWORD>(m_inputReportLength > 0 ? m_inputReportLength : 64));
        DWORD bytesRead = 0;
        ResetEvent(overlapped.hEvent);

        BOOL readSuccess = ReadFile(m_deviceHandle, inputBuffer, bytesToRead, &bytesRead, &overlapped);
        if (!readSuccess) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                DWORD waitRes = WaitForSingleObject(overlapped.hEvent, 100);
                if (waitRes == WAIT_OBJECT_0) {
                    GetOverlappedResult(m_deviceHandle, &overlapped, &bytesRead, FALSE);
                    readSuccess = TRUE;
                } else if (waitRes == WAIT_TIMEOUT) {
                    CancelIo(m_deviceHandle);
                    continue;
                } else {
                    CloseDevice();
                    continue;
                }
            } else if (err == ERROR_DEVICE_NOT_CONNECTED || err == ERROR_HANDLE_EOF || err == ERROR_INVALID_HANDLE) {
                CloseDevice();
                continue;
            } else {
                Sleep(5);
                continue;
            }
        }

        if (readSuccess && bytesRead > 0) {
            ParseInputReport(inputBuffer, bytesRead);
        }

        // Periodic lightbar refresh
        static auto lastRefresh = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRefresh).count() >= 250) {
            lastRefresh = now;
            m_outputDirty = true;
        }

        if (m_outputDirty) {
            SendOutputReport();
        }
    }

    if (overlapped.hEvent) {
        CloseHandle(overlapped.hEvent);
    }
}

void DualShockController::ParseInputReport(const uint8_t* data, size_t size) {
    if (!data || size < 9) return;

    DualShockState newState = {};
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

    if (reportId == 0x11 && size >= 12) {
        // DualShock 4 Extended Bluetooth Report (0x11, 78 bytes)
        m_physicalConnection = DualShockConnection::Bluetooth;
        newState.connection = DualShockConnection::Bluetooth;

        // In BT Report 0x11: byte 1 is flags/header, byte 2 is LX or byte 3 is LX
        // Standard Sony DS4 BT 0x11 report format:
        // Byte 0: 0x11, Byte 1: 0xC0 or 0x80, Byte 2: sub-header
        // If data[1] & 0x80, stick data typically starts at index 3
        int stickBase = 3;
        if (size >= 78) {
            stickBase = 3;
            imuOffset = 15;
            touchOffset = 37;
            batteryOffset = 32;
        } else {
            stickBase = 2;
            imuOffset = 14;
            touchOffset = 36;
            batteryOffset = 31;
        }

        rawStickLX = reportData[stickBase];
        rawStickLY = reportData[stickBase + 1];
        rawStickRX = reportData[stickBase + 2];
        rawStickRY = reportData[stickBase + 3];
        dpadAndShapes = reportData[stickBase + 4];
        btnByte2 = reportData[stickBase + 5];
        btnByte3 = reportData[stickBase + 6];
        rawL2 = reportData[stickBase + 7];
        rawR2 = reportData[stickBase + 8];
    } else if (reportId == 0x01 && size >= 10) {
        // DualShock 4 USB Report (64 bytes) or Basic Bluetooth Report (10 bytes)
        if (size >= 64) {
            m_physicalConnection = DualShockConnection::USB;
            newState.connection = DualShockConnection::USB;
            imuOffset = 13;
            touchOffset = 35;
            batteryOffset = 30;
        } else {
            m_physicalConnection = DualShockConnection::Bluetooth;
            newState.connection = DualShockConnection::Bluetooth;
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
    } else {
        return;
    }

    // 1. Analog Sticks [-1.0f, +1.0f]
    float rawLX = (static_cast<float>(rawStickLX) - 128.0f) / 128.0f;
    float rawLY = -(static_cast<float>(rawStickLY) - 128.0f) / 128.0f;
    float rawRX = (static_cast<float>(rawStickRX) - 128.0f) / 128.0f;
    float rawRY = -(static_cast<float>(rawStickRY) - 128.0f) / 128.0f;

    ApplyDeadzone(rawLX, rawLY, m_stickDeadzone, newState.leftStickX, newState.leftStickY);
    ApplyDeadzone(rawRX, rawRY, m_stickDeadzone, newState.rightStickX, newState.rightStickY);

    // 2. Analog Triggers [0.0f, 1.0f]
    newState.leftTrigger = static_cast<float>(rawL2) / 255.0f;
    newState.rightTrigger = static_cast<float>(rawR2) / 255.0f;
    if (newState.leftTrigger < m_triggerDeadzone) newState.leftTrigger = 0.0f;
    if (newState.rightTrigger < m_triggerDeadzone) newState.rightTrigger = 0.0f;

    // 3. Digital Buttons
    uint32_t buttons = 0;
    uint8_t dpad = dpadAndShapes & 0x0F;
    switch (dpad) {
        case 0: buttons |= DualShockButton::DS_DPadUp; break;
        case 1: buttons |= (DualShockButton::DS_DPadUp | DualShockButton::DS_DPadRight); break;
        case 2: buttons |= DualShockButton::DS_DPadRight; break;
        case 3: buttons |= (DualShockButton::DS_DPadDown | DualShockButton::DS_DPadRight); break;
        case 4: buttons |= DualShockButton::DS_DPadDown; break;
        case 5: buttons |= (DualShockButton::DS_DPadDown | DualShockButton::DS_DPadLeft); break;
        case 6: buttons |= DualShockButton::DS_DPadLeft; break;
        case 7: buttons |= (DualShockButton::DS_DPadUp | DualShockButton::DS_DPadLeft); break;
        default: break;
    }

    if (dpadAndShapes & 0x10) buttons |= DualShockButton::DS_Square;
    if (dpadAndShapes & 0x20) buttons |= DualShockButton::DS_Cross;
    if (dpadAndShapes & 0x40) buttons |= DualShockButton::DS_Circle;
    if (dpadAndShapes & 0x80) buttons |= DualShockButton::DS_Triangle;

    if (btnByte2 & 0x01) buttons |= DualShockButton::DS_L1;
    if (btnByte2 & 0x02) buttons |= DualShockButton::DS_R1;
    if (btnByte2 & 0x04) buttons |= DualShockButton::DS_L2Button;
    if (btnByte2 & 0x08) buttons |= DualShockButton::DS_R2Button;
    if (btnByte2 & 0x10) buttons |= DualShockButton::DS_Share;
    if (btnByte2 & 0x20) buttons |= DualShockButton::DS_Options;
    if (btnByte2 & 0x40) buttons |= DualShockButton::DS_L3;
    if (btnByte2 & 0x80) buttons |= DualShockButton::DS_R3;

    if (btnByte3 & 0x01) buttons |= DualShockButton::DS_PS;
    if (btnByte3 & 0x02) buttons |= DualShockButton::DS_Touchpad;

    newState.buttons = buttons;

    // 4. IMU Sensors (Gyro & Accelerometer)
    if (imuOffset > 0 && static_cast<size_t>(imuOffset + 12) <= size) {
        int16_t gyroP = static_cast<int16_t>(reportData[imuOffset]     | (reportData[imuOffset + 1] << 8));
        int16_t gyroY = static_cast<int16_t>(reportData[imuOffset + 2] | (reportData[imuOffset + 3] << 8));
        int16_t gyroR = static_cast<int16_t>(reportData[imuOffset + 4] | (reportData[imuOffset + 5] << 8));

        int16_t accX = static_cast<int16_t>(reportData[imuOffset + 6]  | (reportData[imuOffset + 7] << 8));
        int16_t accY = static_cast<int16_t>(reportData[imuOffset + 8]  | (reportData[imuOffset + 9] << 8));
        int16_t accZ = static_cast<int16_t>(reportData[imuOffset + 10] | (reportData[imuOffset + 11] << 8));

        const float gyroScale = 2000.0f / 32768.0f;
        newState.motion.gyroPitch = static_cast<float>(gyroP) * gyroScale;
        newState.motion.gyroYaw   = static_cast<float>(gyroY) * gyroScale;
        newState.motion.gyroRoll  = static_cast<float>(gyroR) * gyroScale;

        const float accelScale = 4.0f / 32768.0f;
        newState.motion.accelX = static_cast<float>(accX) * accelScale;
        newState.motion.accelY = static_cast<float>(accY) * accelScale;
        newState.motion.accelZ = static_cast<float>(accZ) * accelScale;
    }

    // 5. Battery Status
    if (batteryOffset > 0 && static_cast<size_t>(batteryOffset) < size) {
        uint8_t battByte = reportData[batteryOffset];
        uint8_t battLevel = (battByte & 0x0F);
        newState.battery.level = (std::min)(100, static_cast<int>(battLevel) * 10);
        newState.battery.isCharging = (battByte & 0x10) != 0;
        newState.battery.isFull = (battByte & 0x20) != 0 || (battLevel >= 10);
    } else {
        newState.battery.level = 100;
        newState.battery.isFull = true;
    }

    // 6. Touchpad Multi-touch (2 points)
    if (touchOffset > 0 && static_cast<size_t>(touchOffset + 8) <= size) {
        const uint8_t* tp = &reportData[touchOffset];
        bool t0Active = !(tp[0] & 0x80);
        uint8_t t0Id = tp[0] & 0x7F;
        uint32_t t0X = tp[1] | ((tp[2] & 0x0F) << 8);
        uint32_t t0Y = ((tp[2] & 0xF0) >> 4) | (tp[3] << 4);

        bool t1Active = !(tp[4] & 0x80);
        uint8_t t1Id = tp[4] & 0x7F;
        uint32_t t1X = tp[5] | ((tp[6] & 0x0F) << 8);
        uint32_t t1Y = ((tp[6] & 0xF0) >> 4) | (tp[7] << 4);

        newState.touch[0].active = t0Active;
        newState.touch[0].id = t0Id;
        newState.touch[0].x = static_cast<float>(t0X) / 1920.0f;
        newState.touch[0].y = static_cast<float>(t0Y) / 942.0f;

        newState.touch[1].active = t1Active;
        newState.touch[1].id = t1Id;
        newState.touch[1].x = static_cast<float>(t1X) / 1920.0f;
        newState.touch[1].y = static_cast<float>(t1Y) / 942.0f;

        // Touchpad as mouse emulation
        if (m_touchpadAsMouse && t0Active) {
            if (m_prevTouchActive) {
                float dx = (newState.touch[0].x - m_prevTouchX) * 1920.0f * m_touchpadSensitivity;
                float dy = (newState.touch[0].y - m_prevTouchY) * 942.0f * m_touchpadSensitivity;
                if (std::abs(dx) > 0.01f || std::abs(dy) > 0.01f) {
                    INPUT mouseInput = {};
                    mouseInput.type = INPUT_MOUSE;
                    mouseInput.mi.dx = static_cast<LONG>(dx);
                    mouseInput.mi.dy = static_cast<LONG>(dy);
                    mouseInput.mi.dwFlags = MOUSEEVENTF_MOVE;
                    SendInput(1, &mouseInput, sizeof(INPUT));
                }
            }
            m_prevTouchX = newState.touch[0].x;
            m_prevTouchY = newState.touch[0].y;
            m_prevTouchActive = true;
        } else {
            m_prevTouchActive = false;
        }

        bool touchClick = (buttons & DualShockButton::DS_Touchpad) != 0;
        if (m_touchpadAsMouse && touchClick != m_prevTouchpadClick) {
            INPUT mouseInput = {};
            mouseInput.type = INPUT_MOUSE;
            mouseInput.mi.dwFlags = touchClick ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            SendInput(1, &mouseInput, sizeof(INPUT));
            m_prevTouchpadClick = touchClick;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_rawState = newState;
    }
}

void DualShockController::SendOutputReport() {
    if (m_deviceHandle == INVALID_HANDLE_VALUE) return;

    std::lock_guard<std::mutex> lock(m_outputMutex);
    m_outputDirty = false;

    if (m_physicalConnection == DualShockConnection::Bluetooth) {
        // Bluetooth Output Report: ID 0x11, 78 bytes total with CRC32
        uint8_t report[78] = {};
        report[0] = 0x11;
        report[1] = 0x80; // Polling rate control
        report[3] = 0x07; // Enable rumble and lightbar

        // Motor vibration
        report[6] = m_smallRumble; // Right light motor
        report[7] = m_heavyRumble; // Left heavy motor

        // Lightbar RGB
        report[8] = m_lightbarR;
        report[9] = m_lightbarG;
        report[10] = m_lightbarB;

        // Compute CRC32 on header 0xA2 + 74 bytes of report
        uint8_t crcData[75];
        crcData[0] = 0xA2; // Bluetooth HID Output Header
        memcpy(&crcData[1], report, 74);
        uint32_t crc = CalculateCRC32(crcData, 75);

        report[74] = static_cast<uint8_t>(crc & 0xFF);
        report[75] = static_cast<uint8_t>((crc >> 8) & 0xFF);
        report[76] = static_cast<uint8_t>((crc >> 16) & 0xFF);
        report[77] = static_cast<uint8_t>((crc >> 24) & 0xFF);

        DWORD bytesWritten = 0;
        WriteFile(m_deviceHandle, report, sizeof(report), &bytesWritten, nullptr);
    } else {
        // USB Output Report: ID 0x05, 32 bytes
        uint8_t report[32] = {};
        report[0] = 0x05;
        report[1] = 0xFF; // Flags: enable rumble and lightbar

        report[4] = m_smallRumble; // Right light motor
        report[5] = m_heavyRumble; // Left heavy motor

        report[6] = m_lightbarR;
        report[7] = m_lightbarG;
        report[8] = m_lightbarB;

        DWORD bytesWritten = 0;
        WriteFile(m_deviceHandle, report, sizeof(report), &bytesWritten, nullptr);
    }
}

#elif defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)

int GetPS4PadHandle();

bool DualShockController::Initialize() {
    int handle = GetPS4PadHandle();
    if (handle >= 0) {
        m_currentState.connection = DualShockConnection::PS4Native;
        return true;
    }
    return false;
}

void DualShockController::Shutdown() {
    m_currentState.connection = DualShockConnection::Disconnected;
}

#else

bool DualShockController::Initialize() { return false; }
void DualShockController::Shutdown() {}

#endif

void DualShockController::Update(float dt) {
    m_previousState = m_currentState;

#if defined(_WIN32)
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_currentState = m_rawState;
    }
#elif defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    int handle = GetPS4PadHandle();
    if (handle < 0) {
        m_currentState.connection = DualShockConnection::Disconnected;
        return;
    }

    OrbisPadData padData = {};
    int rc = scePadReadState(handle, &padData);
    if (rc != 0) {
        m_currentState.connection = DualShockConnection::Disconnected;
        return;
    }

    bool isConnected = (padData.connected != 0) ||
                       (padData.leftStick.x != 0 || padData.leftStick.y != 0 ||
                        padData.rightStick.x != 0 || padData.rightStick.y != 0 ||
                        padData.buttons != 0);
    if (!isConnected) {
        m_currentState.connection = DualShockConnection::Disconnected;
        m_currentState.leftStickX = 0.0f;
        m_currentState.leftStickY = 0.0f;
        m_currentState.rightStickX = 0.0f;
        m_currentState.rightStickY = 0.0f;
        return;
    }

    m_currentState.connection = DualShockConnection::PS4Native;

    // 1. Digital buttons
    uint32_t buttons = 0;
    if (padData.buttons & ORBIS_PAD_BUTTON_CROSS)      buttons |= DualShockButton::DS_Cross;
    if (padData.buttons & ORBIS_PAD_BUTTON_CIRCLE)     buttons |= DualShockButton::DS_Circle;
    if (padData.buttons & ORBIS_PAD_BUTTON_SQUARE)     buttons |= DualShockButton::DS_Square;
    if (padData.buttons & ORBIS_PAD_BUTTON_TRIANGLE)   buttons |= DualShockButton::DS_Triangle;
    if (padData.buttons & ORBIS_PAD_BUTTON_UP)         buttons |= DualShockButton::DS_DPadUp;
    if (padData.buttons & ORBIS_PAD_BUTTON_RIGHT)      buttons |= DualShockButton::DS_DPadRight;
    if (padData.buttons & ORBIS_PAD_BUTTON_DOWN)       buttons |= DualShockButton::DS_DPadDown;
    if (padData.buttons & ORBIS_PAD_BUTTON_LEFT)       buttons |= DualShockButton::DS_DPadLeft;
    if (padData.buttons & ORBIS_PAD_BUTTON_L1)         buttons |= DualShockButton::DS_L1;
    if (padData.buttons & ORBIS_PAD_BUTTON_R1)         buttons |= DualShockButton::DS_R1;
    if (padData.buttons & ORBIS_PAD_BUTTON_L2)         buttons |= DualShockButton::DS_L2Button;
    if (padData.buttons & ORBIS_PAD_BUTTON_R2)         buttons |= DualShockButton::DS_R2Button;
    if (padData.buttons & ORBIS_PAD_BUTTON_L3)         buttons |= DualShockButton::DS_L3;
    if (padData.buttons & ORBIS_PAD_BUTTON_R3)         buttons |= DualShockButton::DS_R3;
    if (padData.buttons & ORBIS_PAD_BUTTON_OPTIONS)    buttons |= DualShockButton::DS_Options;
    if (padData.buttons & ORBIS_PAD_BUTTON_TOUCH_PAD)  buttons |= DualShockButton::DS_Touchpad;

    m_currentState.buttons = buttons;

    // 2. Analog Sticks [-1.0f .. 1.0f]
    m_currentState.leftStickX  = (static_cast<float>(padData.leftStick.x) - 128.0f) / 128.0f;
    m_currentState.leftStickY  = -(static_cast<float>(padData.leftStick.y) - 128.0f) / 128.0f;
    m_currentState.rightStickX = (static_cast<float>(padData.rightStick.x) - 128.0f) / 128.0f;
    m_currentState.rightStickY = -(static_cast<float>(padData.rightStick.y) - 128.0f) / 128.0f;

    // Deadzones
    if (std::abs(m_currentState.leftStickX) < m_stickDeadzone) m_currentState.leftStickX = 0.0f;
    if (std::abs(m_currentState.leftStickY) < m_stickDeadzone) m_currentState.leftStickY = 0.0f;
    if (std::abs(m_currentState.rightStickX) < m_stickDeadzone) m_currentState.rightStickX = 0.0f;
    if (std::abs(m_currentState.rightStickY) < m_stickDeadzone) m_currentState.rightStickY = 0.0f;

    // 3. Triggers [0.0f .. 1.0f]
    m_currentState.leftTrigger  = static_cast<float>(padData.analogButtons.l2) / 255.0f;
    m_currentState.rightTrigger = static_cast<float>(padData.analogButtons.r2) / 255.0f;
    if (m_currentState.leftTrigger < m_triggerDeadzone) m_currentState.leftTrigger = 0.0f;
    if (m_currentState.rightTrigger < m_triggerDeadzone) m_currentState.rightTrigger = 0.0f;
#endif

    // Timed rumble update
    if (m_rumbleTimer > 0.0f) {
        m_rumbleTimer -= dt;
        if (m_rumbleTimer <= 0.0f) {
            SetRumble(0.0f, 0.0f);
        }
    }
}

bool DualShockController::IsButtonDown(DualShockButton button) const {
    return (m_currentState.buttons & static_cast<uint32_t>(button)) != 0;
}

bool DualShockController::IsButtonPressed(DualShockButton button) const {
    uint32_t mask = static_cast<uint32_t>(button);
    return (m_currentState.buttons & mask) != 0 && (m_previousState.buttons & mask) == 0;
}

bool DualShockController::IsButtonReleased(DualShockButton button) const {
    uint32_t mask = static_cast<uint32_t>(button);
    return (m_currentState.buttons & mask) == 0 && (m_previousState.buttons & mask) != 0;
}

void DualShockController::SetRumble(float heavyMotor, float smallMotor) {
    uint8_t heavy = static_cast<uint8_t>((std::clamp)(heavyMotor * 255.0f, 0.0f, 255.0f));
    uint8_t small = static_cast<uint8_t>((std::clamp)(smallMotor * 255.0f, 0.0f, 255.0f));

    if (heavy != m_heavyRumble || small != m_smallRumble) {
        m_heavyRumble = heavy;
        m_smallRumble = small;
        m_outputDirty = true;

#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
        int handle = GetPS4PadHandle();
        if (handle >= 0) {
            OrbisPadVibeParam vib = {};
            vib.lgMotor = heavy;
            vib.smMotor = small;
            scePadSetVibration(handle, &vib);
        }
#endif
    }
}

void DualShockController::PlayRumble(float heavyMotor, float smallMotor, float durationSeconds) {
    SetRumble(heavyMotor, smallMotor);
    m_rumbleTimer = durationSeconds;
}

void DualShockController::SetLightbar(uint8_t r, uint8_t g, uint8_t b) {
    if (m_lightbarR != r || m_lightbarG != g || m_lightbarB != b) {
        m_lightbarR = r;
        m_lightbarG = g;
        m_lightbarB = b;
        m_outputDirty = true;

#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
        int handle = GetPS4PadHandle();
        if (handle >= 0) {
            OrbisPadColor color = {};
            color.r = r;
            color.g = g;
            color.b = b;
            scePadSetLightBar(handle, &color);
        }
#endif
    }
}

void DualShockController::SetLightbarColor(float r, float g, float b) {
    SetLightbar(
        static_cast<uint8_t>((std::clamp)(r * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>((std::clamp)(g * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>((std::clamp)(b * 255.0f, 0.0f, 255.0f))
    );
}

void DualShockController::TurnOffAll() {
    SetRumble(0.0f, 0.0f);
    SetLightbar(0, 0, 0);
}

void DualShockController::SetDeadzone(float stickDeadzone, float triggerDeadzone) {
    m_stickDeadzone = (std::clamp)(stickDeadzone, 0.0f, 0.9f);
    m_triggerDeadzone = (std::clamp)(triggerDeadzone, 0.0f, 0.5f);
}

void DualShockController::ApplyDeadzone(float rawX, float rawY, float deadzone, float& outX, float& outY) {
    float mag = std::sqrt(rawX * rawX + rawY * rawY);
    if (mag < deadzone) {
        outX = 0.0f;
        outY = 0.0f;
    } else {
        float norm = (mag - deadzone) / (1.0f - deadzone);
        norm = (std::min)(1.0f, norm);
        outX = (rawX / mag) * norm;
        outY = (rawY / mag) * norm;
    }
}

} // namespace EngineInput
