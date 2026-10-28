#include "DualSenseAudio.h"

#if defined(_WIN32)
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <iostream>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <random>

#pragma comment(lib, "ole32.lib")

namespace EngineAudio {

// PKEY_Device_FriendlyName defined via Functiondiscoverykeys_devpkey.h or fallback
#ifndef PKEY_Device_FriendlyName
static const PROPERTYKEY PKEY_Device_FriendlyName = {
    { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14
};
#endif

DualSenseAudioSystem& DualSenseAudioSystem::Get() {
    static DualSenseAudioSystem s_instance;
    return s_instance;
}

DualSenseAudioSystem::DualSenseAudioSystem() {
}

DualSenseAudioSystem::~DualSenseAudioSystem() {
    Shutdown();
}

bool DualSenseAudioSystem::Initialize() {
    if (m_running.load()) return true;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr) || hr == S_FALSE) {
        m_comInitialized = true;
    }

    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&m_deviceEnumerator)
    );

    if (FAILED(hr) || !m_deviceEnumerator) {
        return false;
    }

    RefreshAudioDevices();

    // Priority 1: Auto-select DualSense / Wireless Controller audio device (USB or Bluetooth)
    bool opened = false;
    for (size_t i = 0; i < m_devices.size(); ++i) {
        if (m_devices[i].isDualSense) {
            if (SetSelectedDevice(static_cast<int>(i))) {
                opened = true;
                break;
            }
        }
    }

    // Priority 2: If DualSense audio endpoint failed or not present (e.g. Bluetooth controller using standard PC/headset audio), select Default System Audio Device
    if (!opened) {
        for (size_t i = 0; i < m_devices.size(); ++i) {
            if (m_devices[i].isDefault) {
                if (SetSelectedDevice(static_cast<int>(i))) {
                    opened = true;
                    break;
                }
            }
        }
    }

    // Priority 3: Fallback to ANY working active audio endpoint
    if (!opened) {
        for (size_t i = 0; i < m_devices.size(); ++i) {
            if (SetSelectedDevice(static_cast<int>(i))) {
                opened = true;
                break;
            }
        }
    }

    // Priority 4: Fallback to direct default endpoint
    if (!opened) {
        opened = OpenAudioDevice(L"");
    }

    return opened;
}

void DualSenseAudioSystem::Shutdown() {
    CloseAudioDevice();

    if (m_deviceEnumerator) {
        m_deviceEnumerator->Release();
        m_deviceEnumerator = nullptr;
    }

    if (m_comInitialized) {
        CoUninitialize();
        m_comInitialized = false;
    }
}

void DualSenseAudioSystem::RefreshAudioDevices() {
    m_devices.clear();
    if (!m_deviceEnumerator) return;

    IMMDeviceCollection* pCollection = nullptr;
    HRESULT hr = m_deviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
    if (FAILED(hr) || !pCollection) return;

    UINT count = 0;
    pCollection->GetCount(&count);

    IMMDevice* pDefaultDevice = nullptr;
    LPWSTR defaultId = nullptr;
    if (SUCCEEDED(m_deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDefaultDevice)) && pDefaultDevice) {
        pDefaultDevice->GetId(&defaultId);
        pDefaultDevice->Release();
    }

    for (UINT i = 0; i < count; ++i) {
        IMMDevice* pEndpoint = nullptr;
        if (FAILED(pCollection->Item(i, &pEndpoint)) || !pEndpoint) continue;

        LPWSTR pId = nullptr;
        pEndpoint->GetId(&pId);

        IPropertyStore* pProps = nullptr;
        std::string friendlyName = "Unknown Audio Device";
        bool isDualSense = false;
        bool isBluetooth = false;

        if (SUCCEEDED(pEndpoint->OpenPropertyStore(STGM_READ, &pProps)) && pProps) {
            PROPVARIANT varName;
            PropVariantInit(&varName);
            if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &varName))) {
                if (varName.vt == VT_LPWSTR && varName.pwszVal) {
                    int len = WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0) {
                        friendlyName.resize(len - 1);
                        WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, &friendlyName[0], len, nullptr, nullptr);
                    }
                }
            }
            PropVariantClear(&varName);
            pProps->Release();
        }

        // Check if device name or endpoint ID corresponds to DualSense / Wireless Controller / Bluetooth
        std::string lowerName = friendlyName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

        std::wstring lowerId = pId ? pId : L"";
        std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(), ::towlower);

        if (lowerName.find("wireless controller") != std::string::npos ||
            lowerName.find("dualsense") != std::string::npos ||
            lowerName.find("playstation") != std::string::npos ||
            lowerName.find("sony") != std::string::npos ||
            lowerName.find("беспроводной контроллер") != std::string::npos ||
            lowerName.find("контроллер") != std::string::npos ||
            lowerId.find(L"054c") != std::wstring::npos ||
            lowerId.find(L"0ce6") != std::wstring::npos ||
            lowerId.find(L"0df2") != std::wstring::npos) {
            isDualSense = true;
        }

        // Comprehensive Bluetooth detection: BT audio headsets, headphones, earbuds, speakers
        // Exclude DualSense controllers so the DualSense audio endpoint isn't misidentified as a generic BT headset
        if (!isDualSense && (
            lowerName.find("bluetooth") != std::string::npos ||
            lowerName.find("hands-free") != std::string::npos ||
            lowerName.find("hands free") != std::string::npos ||
            lowerName.find("блютуз") != std::string::npos ||
            lowerName.find("беспроводн") != std::string::npos ||
            lowerName.find("wireless") != std::string::npos ||
            lowerName.find("g435") != std::string::npos ||
            lowerName.find("g433") != std::string::npos ||
            lowerName.find("g933") != std::string::npos ||
            lowerName.find("g935") != std::string::npos ||
            lowerName.find("airpods") != std::string::npos ||
            lowerName.find("air pods") != std::string::npos ||
            lowerName.find("galaxy buds") != std::string::npos ||
            lowerName.find("freebuds") != std::string::npos ||
            lowerName.find("wh-1000") != std::string::npos ||
            lowerName.find("wf-1000") != std::string::npos ||
            lowerName.find("wh-ch") != std::string::npos ||
            lowerName.find("headset audio") != std::string::npos ||
            lowerName.find("наушники гарнитуры") != std::string::npos ||
            lowerName.find("наушники") != std::string::npos ||
            lowerName.find("гарнитуры") != std::string::npos ||
            lowerId.find(L"bth") != std::wstring::npos ||
            lowerId.find(L"bthhfenum") != std::wstring::npos ||
            lowerId.find(L"bthenum") != std::wstring::npos)) {
            isBluetooth = true;
        }

        // Determine bus type string
        std::string busType = "Audio";
        if (isBluetooth) busType = "Bluetooth";
        else if (lowerId.find(L"usb") != std::wstring::npos) busType = "USB";
        else if (lowerId.find(L"hdaudio") != std::wstring::npos) busType = "Internal";

        bool isDef = false;
        if (defaultId && pId && wcscmp(defaultId, pId) == 0) {
            isDef = true;
        }

        AudioDeviceDesc desc;
        desc.id = pId ? pId : L"";
        desc.name = friendlyName;
        desc.isDualSense = isDualSense;
        desc.isBluetooth = isBluetooth;
        desc.isDefault = isDef;
        desc.busType = busType;
        m_devices.push_back(desc);

        if (pId) CoTaskMemFree(pId);
        pEndpoint->Release();
    }

    if (defaultId) CoTaskMemFree(defaultId);
    pCollection->Release();
}

bool DualSenseAudioSystem::SetSelectedDevice(int index) {
    if (index < 0 || index >= static_cast<int>(m_devices.size())) return false;

    CloseAudioDevice();
    m_selectedDeviceIndex = index;

    const auto& desc = m_devices[index];
    m_currentDeviceName = desc.name;
    m_isDualSenseDevice = desc.isDualSense;
    m_isBluetoothDevice = desc.isBluetooth;

    return OpenAudioDevice(desc.id);
}

bool DualSenseAudioSystem::SelectDefaultDevice() {
    RefreshAudioDevices();
    for (size_t i = 0; i < m_devices.size(); ++i) {
        if (m_devices[i].isDefault) {
            return SetSelectedDevice(static_cast<int>(i));
        }
    }
    if (!m_devices.empty()) {
        return SetSelectedDevice(0);
    }
    return OpenAudioDevice(L"");
}

bool DualSenseAudioSystem::SelectBluetoothDevice() {
    RefreshAudioDevices();
    // Priority 1: Prefer non-DualSense Bluetooth headset (e.g. G435, AirPods)
    for (size_t i = 0; i < m_devices.size(); ++i) {
        if (m_devices[i].isBluetooth && !m_devices[i].isDualSense) {
            if (SetSelectedDevice(static_cast<int>(i))) return true;
        }
    }
    // Priority 2: Any Bluetooth device
    for (size_t i = 0; i < m_devices.size(); ++i) {
        if (m_devices[i].isBluetooth) {
            if (SetSelectedDevice(static_cast<int>(i))) return true;
        }
    }
    // Priority 3: Fallback to default
    return SelectDefaultDevice();
}

bool DualSenseAudioSystem::IsCurrentDeviceBluetooth() const {
    return m_isBluetoothDevice;
}

void DualSenseAudioSystem::SetBluetoothBufferDurationMs(int ms) {
    if (ms < 50) ms = 50;
    if (ms > 200) ms = 200;
    m_bluetoothBufferMs = ms;
    // If currently using Bluetooth, reopen device with new buffer size
    if (m_isBluetoothDevice && m_selectedDeviceIndex >= 0 &&
        m_selectedDeviceIndex < static_cast<int>(m_devices.size())) {
        SetSelectedDevice(m_selectedDeviceIndex);
    }
}

bool DualSenseAudioSystem::OpenAudioDevice(const std::wstring& deviceId) {
    if (!m_deviceEnumerator) return false;

    CloseAudioDevice();

    HRESULT hr = S_OK;
    if (deviceId.empty()) {
        hr = m_deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_audioDevice);
    } else {
        hr = m_deviceEnumerator->GetDevice(deviceId.c_str(), &m_audioDevice);
        if (FAILED(hr) || !m_audioDevice) {
            // Fallback to default audio endpoint if specified endpoint ID is unavailable
            hr = m_deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_audioDevice);
        }
    }

    if (FAILED(hr) || !m_audioDevice) return false;

    hr = m_audioDevice->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(&m_audioClient)
    );

    if (FAILED(hr) || !m_audioClient) {
        CloseAudioDevice();
        return false;
    }

    WAVEFORMATEX* pMixFormat = nullptr;
    hr = m_audioClient->GetMixFormat(&pMixFormat);
    if (FAILED(hr) || !pMixFormat) {
        CloseAudioDevice();
        return false;
    }

    // Record sample rate and channel count directly from the device's native mix format
    m_sampleRate = (pMixFormat->nSamplesPerSec > 0) ? pMixFormat->nSamplesPerSec : 48000;
    m_deviceChannels = (pMixFormat->nChannels > 0) ? pMixFormat->nChannels : 2;
    m_bitsPerSample = (pMixFormat->wBitsPerSample > 0) ? pMixFormat->wBitsPerSample : 32;

    m_isFloatFormat = false;
    if (pMixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        m_isFloatFormat = true;
    } else if (pMixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE* pExt = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pMixFormat);
        if (IsEqualGUID(pExt->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            m_isFloatFormat = true;
        } else if (IsEqualGUID(pExt->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
            m_isFloatFormat = false;
        }
    } else if (pMixFormat->wFormatTag == WAVE_FORMAT_PCM) {
        m_isFloatFormat = false;
    } else if (pMixFormat->wBitsPerSample == 32) {
        m_isFloatFormat = true;
    }

    // Copy full structure for reference
    if (pMixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        memcpy(&m_waveFormat, pMixFormat, sizeof(WAVEFORMATEXTENSIBLE));
    } else {
        memset(&m_waveFormat, 0, sizeof(WAVEFORMATEXTENSIBLE));
        m_waveFormat.Format = *pMixFormat;
    }

    // Adaptive buffer: larger for Bluetooth to prevent dropouts, 50ms for wired/USB
    int bufMs = (m_isBluetoothDevice && m_bluetoothAudioEnabled) ? m_bluetoothBufferMs : 50;
    if (bufMs < 50) bufMs = 50;
    REFERENCE_TIME hnsBufferDuration = static_cast<REFERENCE_TIME>(bufMs) * 10000LL; // ms -> 100ns units
    bool useEvent = true;

    // Try Event Callback mode first
    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        hnsBufferDuration,
        0,
        pMixFormat,
        nullptr
    );

    if (FAILED(hr)) {
        // Must release and re-activate a clean IAudioClient instance for polling fallback
        m_audioClient->Release();
        m_audioClient = nullptr;

        hr = m_audioDevice->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(&m_audioClient)
        );

        if (SUCCEEDED(hr) && m_audioClient) {
            hr = m_audioClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                0,
                hnsBufferDuration,
                0,
                pMixFormat,
                nullptr
            );
            useEvent = false;
        }
    }

    CoTaskMemFree(pMixFormat);

    if (FAILED(hr) || !m_audioClient) {
        CloseAudioDevice();
        return false;
    }

    if (useEvent) {
        m_audioEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_audioEvent) {
            hr = m_audioClient->SetEventHandle(m_audioEvent);
            if (FAILED(hr)) {
                CloseHandle(m_audioEvent);
                m_audioEvent = nullptr;
            }
        }
    }

    hr = m_audioClient->GetBufferSize(&m_bufferFrameCount);
    if (FAILED(hr)) {
        CloseAudioDevice();
        return false;
    }

    hr = m_audioClient->GetService(
        __uuidof(IAudioRenderClient),
        reinterpret_cast<void**>(&m_renderClient)
    );

    if (FAILED(hr) || !m_renderClient) {
        CloseAudioDevice();
        return false;
    }

    // Pre-fill buffer with silence so the device doesn't glitch at start
    BYTE* pInitialData = nullptr;
    if (SUCCEEDED(m_renderClient->GetBuffer(m_bufferFrameCount, &pInitialData)) && pInitialData) {
        memset(pInitialData, 0, (size_t)m_bufferFrameCount * m_deviceChannels * (m_bitsPerSample / 8));
        m_renderClient->ReleaseBuffer(m_bufferFrameCount, 0);
    }

    hr = m_audioClient->Start();
    if (FAILED(hr)) {
        CloseAudioDevice();
        return false;
    }

    m_running.store(true);
    m_audioThread = std::thread(&DualSenseAudioSystem::AudioThread, this);

    return true;
}

void DualSenseAudioSystem::CloseAudioDevice() {
    if (m_running.load()) {
        m_running.store(false);
        if (m_audioEvent) {
            SetEvent(m_audioEvent);
        }
        if (m_audioThread.joinable()) {
            m_audioThread.join();
        }
    }

    if (m_audioClient) {
        m_audioClient->Stop();
    }

    if (m_renderClient) {
        m_renderClient->Release();
        m_renderClient = nullptr;
    }

    if (m_audioClient) {
        m_audioClient->Release();
        m_audioClient = nullptr;
    }

    if (m_audioDevice) {
        m_audioDevice->Release();
        m_audioDevice = nullptr;
    }

    if (m_audioEvent) {
        CloseHandle(m_audioEvent);
        m_audioEvent = nullptr;
    }
}

void DualSenseAudioSystem::AudioThread() {
    // Initialize COM on this worker thread
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    std::vector<float> mixBuffer;

    while (m_running.load()) {
        if (m_audioEvent) {
            DWORD waitRes = WaitForSingleObject(m_audioEvent, 50);
            if (!m_running.load()) break;
            if (waitRes != WAIT_OBJECT_0 && waitRes != WAIT_TIMEOUT) continue;
        } else {
            Sleep(10);
            if (!m_running.load()) break;
        }

        if (!m_audioClient || !m_renderClient) break;

        UINT32 padding = 0;
        HRESULT hr = m_audioClient->GetCurrentPadding(&padding);
        if (FAILED(hr)) continue;

        UINT32 framesAvailable = (m_bufferFrameCount > padding) ? (m_bufferFrameCount - padding) : 0;
        if (framesAvailable == 0) continue;

        BYTE* pData = nullptr;
        hr = m_renderClient->GetBuffer(framesAvailable, &pData);
        if (FAILED(hr) || !pData) continue;

        size_t totalSamples = static_cast<size_t>(framesAvailable) * m_deviceChannels;
        if (mixBuffer.size() < totalSamples) {
            mixBuffer.resize(totalSamples);
        }
        std::fill(mixBuffer.begin(), mixBuffer.begin() + totalSamples, 0.0f);

        MixAudio(mixBuffer.data(), framesAvailable);

        // Convert mixed float buffer to hardware format
        if (m_isFloatFormat || m_bitsPerSample == 32) {
            float* pOutFloat = reinterpret_cast<float*>(pData);
            for (size_t i = 0; i < totalSamples; ++i) {
                pOutFloat[i] = std::clamp(mixBuffer[i], -1.0f, 1.0f);
            }
        } else if (m_bitsPerSample == 16) {
            int16_t* pOut16 = reinterpret_cast<int16_t*>(pData);
            for (size_t i = 0; i < totalSamples; ++i) {
                float sample = std::clamp(mixBuffer[i], -1.0f, 1.0f);
                pOut16[i] = static_cast<int16_t>(sample * 32767.0f);
            }
        } else if (m_bitsPerSample == 24) {
            uint8_t* pOut = pData;
            for (size_t i = 0; i < totalSamples; ++i) {
                float sample = std::clamp(mixBuffer[i], -1.0f, 1.0f);
                int32_t val = static_cast<int32_t>(sample * 8388607.0f);
                pOut[i * 3 + 0] = static_cast<uint8_t>(val & 0xFF);
                pOut[i * 3 + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
                pOut[i * 3 + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
            }
        }

        m_renderClient->ReleaseBuffer(framesAvailable, 0);
    }

    if (SUCCEEDED(hrCo)) {
        CoUninitialize();
    }
}

void DualSenseAudioSystem::MixAudio(float* outputBuffer, size_t frameCount) {
    std::lock_guard<std::mutex> lock(m_soundMutex);

    if (m_activeSounds.empty() || m_sampleRate <= 0 || m_deviceChannels <= 0) {
        float prev = m_audioEnergy.load();
        if (prev > 0.001f) m_audioEnergy.store(prev * 0.85f);
        else m_audioEnergy.store(0.0f);
        return;
    }

    for (auto& sound : m_activeSounds) {
        if (sound.isFinished || sound.pcmSamples.empty()) continue;

        float effectivePitch = (sound.pitch > 0.05f) ? sound.pitch : 1.0f;
        float step = (static_cast<float>(sound.sampleRate) / static_cast<float>(m_sampleRate)) * effectivePitch;
        size_t totalSamples = sound.pcmSamples.size();

        for (size_t frame = 0; frame < frameCount; ++frame) {
            float curPos = sound.subPlayhead;
            size_t idx0 = static_cast<size_t>(curPos);

            if (sound.channels == 1) {
                if (idx0 >= totalSamples) {
                    if (sound.loop && totalSamples > 0) {
                        sound.subPlayhead = std::fmod(curPos, static_cast<float>(totalSamples));
                        curPos = sound.subPlayhead;
                        idx0 = static_cast<size_t>(curPos);
                    } else {
                        sound.isFinished = true;
                        break;
                    }
                }

                size_t idx1 = (idx0 + 1 < totalSamples) ? (idx0 + 1) : (sound.loop ? 0 : idx0);
                float frac = curPos - static_cast<float>(idx0);
                float s0 = sound.pcmSamples[idx0];
                float s1 = sound.pcmSamples[idx1];
                float s = (s0 + (s1 - s0) * frac) * sound.volume * m_masterVolume;

                float sampleL = s;
                float sampleR = s;

                size_t outOffset = frame * m_deviceChannels;
                if (m_deviceChannels >= 4) {
                    switch (m_channelRouting) {
                        case DualSenseChannelRouting::SpeakerOnly:
                        case DualSenseChannelRouting::StereoMain:
                            // Channels 0 & 1 = DualSense hardware speaker & 3.5mm headphone jack
                            outputBuffer[outOffset + 0] += sampleL;
                            outputBuffer[outOffset + 1] += sampleR;
                            break;
                        case DualSenseChannelRouting::AllChannels:
                        case DualSenseChannelRouting::Auto:
                        default:
                            // Channels 0 & 1 = Hardware speaker/headphones
                            // Channels 2 & 3 = Haptic voice coil actuators (dual audio-vibration)
                            outputBuffer[outOffset + 0] += sampleL;
                            outputBuffer[outOffset + 1] += sampleR;
                            outputBuffer[outOffset + 2] += sampleL;
                            outputBuffer[outOffset + 3] += sampleR;
                            break;
                    }
                } else if (m_deviceChannels == 2) {
                    outputBuffer[outOffset + 0] += sampleL;
                    outputBuffer[outOffset + 1] += sampleR;
                } else if (m_deviceChannels == 1) {
                    outputBuffer[outOffset + 0] += s;
                } else {
                    for (int c = 0; c < m_deviceChannels; ++c) {
                        outputBuffer[outOffset + c] += (c % 2 == 0) ? sampleL : sampleR;
                    }
                }

                sound.subPlayhead += step;
            } else {
                // Stereo sound input
                size_t frameIdx0 = idx0 * 2;
                if (frameIdx0 + 1 >= totalSamples) {
                    if (sound.loop && totalSamples >= 2) {
                        size_t totalFrames = totalSamples / 2;
                        sound.subPlayhead = std::fmod(curPos, static_cast<float>(totalFrames));
                        curPos = sound.subPlayhead;
                        idx0 = static_cast<size_t>(curPos);
                        frameIdx0 = idx0 * 2;
                    } else {
                        sound.isFinished = true;
                        break;
                    }
                }

                size_t nextIdx = (idx0 + 1) * 2;
                if (nextIdx + 1 >= totalSamples) {
                    nextIdx = sound.loop ? 0 : frameIdx0;
                }

                float frac = curPos - static_cast<float>(idx0);
                float s0L = sound.pcmSamples[frameIdx0];
                float s0R = sound.pcmSamples[frameIdx0 + 1];
                float s1L = sound.pcmSamples[nextIdx];
                float s1R = sound.pcmSamples[nextIdx + 1];

                float sampleL = (s0L + (s1L - s0L) * frac) * sound.volume * m_masterVolume;
                float sampleR = (s0R + (s1R - s0R) * frac) * sound.volume * m_masterVolume;

                size_t outOffset = frame * m_deviceChannels;
                if (m_deviceChannels >= 4) {
                    switch (m_channelRouting) {
                        case DualSenseChannelRouting::SpeakerOnly:
                        case DualSenseChannelRouting::StereoMain:
                            // Channels 0 & 1 = DualSense hardware speaker & 3.5mm headphone jack
                            outputBuffer[outOffset + 0] += sampleL;
                            outputBuffer[outOffset + 1] += sampleR;
                            break;
                        case DualSenseChannelRouting::AllChannels:
                        case DualSenseChannelRouting::Auto:
                        default:
                            // Channels 0 & 1 = Hardware speaker/headphones
                            // Channels 2 & 3 = Haptic voice coil actuators (dual audio-vibration)
                            outputBuffer[outOffset + 0] += sampleL;
                            outputBuffer[outOffset + 1] += sampleR;
                            outputBuffer[outOffset + 2] += sampleL;
                            outputBuffer[outOffset + 3] += sampleR;
                            break;
                    }
                } else if (m_deviceChannels == 2) {
                    outputBuffer[outOffset + 0] += sampleL;
                    outputBuffer[outOffset + 1] += sampleR;
                } else if (m_deviceChannels == 1) {
                    outputBuffer[outOffset + 0] += (sampleL + sampleR) * 0.5f;
                } else {
                    for (int c = 0; c < m_deviceChannels; ++c) {
                        outputBuffer[outOffset + c] += (c % 2 == 0) ? sampleL : sampleR;
                    }
                }

                sound.subPlayhead += step;
            }
        }
    }

    // Clean finished sounds
    m_activeSounds.erase(
        std::remove_if(m_activeSounds.begin(), m_activeSounds.end(), [](const ActiveSound& s) { return s.isFinished; }),
        m_activeSounds.end()
    );

    size_t totalSamples = frameCount * m_deviceChannels;

    // -----------------------------------------------------------------------
    // DualSense Speaker Acoustic Simulation (Bluetooth mode)
    // Applies a bandpass filter mimicking the DualSense internal speaker
    // response: roll-off below ~200Hz and above ~6.5kHz with 3kHz resonance
    // -----------------------------------------------------------------------
    if (m_isBluetoothDevice && m_bluetoothAudioEnabled && m_bluetoothSpeakerSim && m_sampleRate > 0) {
        // Simple 2-pole IIR bandpass simulation stored as thread-local state
        // HP cutoff ~200Hz, LP cutoff ~6500Hz + mid-resonance boost at ~3kHz
        static float s_hp1 = 0.0f, s_hp2 = 0.0f;  // high-pass filter state (remove sub-bass)
        static float s_lp1 = 0.0f, s_lp2 = 0.0f;  // low-pass filter state (remove ultra-highs)

        const float dt = 1.0f / static_cast<float>(m_sampleRate);
        // HP: 1st order, cutoff ~200Hz
        const float hpAlpha = 1.0f / (1.0f + 2.0f * 3.14159265f * 200.0f * dt);
        // LP: 1st order, cutoff ~6500Hz
        const float lpAlpha = 2.0f * 3.14159265f * 6500.0f * dt / (1.0f + 2.0f * 3.14159265f * 6500.0f * dt);
        // Mid resonance boost at ~3kHz (slight presence/punch boost)
        const float resFreq = 3000.0f;
        const float resOmega = 2.0f * 3.14159265f * resFreq * dt;
        const float resGain = 1.4f; // +3dB presence boost

        for (size_t i = 0; i < totalSamples; ++i) {
            float x = outputBuffer[i];
            // High-pass: remove deep sub-bass (< 200Hz)
            float hp = hpAlpha * (s_hp1 + x - (i > 0 ? outputBuffer[i - 1] : x));
            s_hp1 = hp;
            // Low-pass: remove ultra-high frequencies (> 6.5kHz)
            s_lp1 = s_lp1 + lpAlpha * (hp - s_lp1);
            // Mid resonance: add sine-approximated resonance bump at 3kHz
            float resBump = std::sin(resOmega * static_cast<float>(i)) * (s_lp1 * (resGain - 1.0f) * 0.15f);
            outputBuffer[i] = s_lp1 + resBump;
        }
    }

    // -----------------------------------------------------------------------
    // Compute peak audio energy for Audio-to-Haptics and Audio-Reactive Lightbar
    // Also compute dual-band (bass / treble) energy for directional haptics
    // Bass: low frequencies (<160Hz cutoff) -> left heavy rumble motor
    // Treble: mid/high frequencies (remainder) -> right light rumble motor
    // -----------------------------------------------------------------------
    float peakEnergy = 0.0f;
    float peakBass = 0.0f;
    float peakTreble = 0.0f;

    // Simple 1-pole IIR low-pass at ~160Hz for bass extraction
    static float s_bassLp = 0.0f;
    const float bassAlpha = (m_sampleRate > 0)
        ? (2.0f * 3.14159265f * 160.0f / static_cast<float>(m_sampleRate))
          / (1.0f + 2.0f * 3.14159265f * 160.0f / static_cast<float>(m_sampleRate))
        : 0.02f;

    for (size_t i = 0; i < totalSamples; i += m_deviceChannels) {
        // Use left channel (index 0) for energy analysis
        float sample = outputBuffer[i];
        float absSample = std::abs(sample);
        if (absSample > peakEnergy) peakEnergy = absSample;

        // Bass component via low-pass
        s_bassLp = s_bassLp + bassAlpha * (sample - s_bassLp);
        float bassAbs = std::abs(s_bassLp);
        if (bassAbs > peakBass) peakBass = bassAbs;

        // Treble = total - bass
        float trebleAbs = std::abs(sample - s_bassLp);
        if (trebleAbs > peakTreble) peakTreble = trebleAbs;
    }

    // Smooth energy values with different decay rates
    float prevE = m_audioEnergy.load();
    m_audioEnergy.store(prevE * 0.65f + peakEnergy * 0.35f);

    float prevB = m_bassEnergy.load();
    m_bassEnergy.store(prevB * 0.60f + peakBass * 0.40f);

    float prevT = m_trebleEnergy.load();
    m_trebleEnergy.store(prevT * 0.55f + peakTreble * 0.45f);
}

// Procedural Sound Synthesizers optimized for DualSense Speaker
std::vector<float> DualSenseAudioSystem::GenerateShootPcm(int sampleRate) {
    // Punchy gunshot: fast attack noise burst + decaying punch + metallic snap
    int length = static_cast<int>(sampleRate * 0.22f); // 220ms
    std::vector<float> pcm(length);

    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    for (int i = 0; i < length; ++i) {
        float t = static_cast<float>(i) / sampleRate;

        // Pitch envelope: slides from 1100Hz down to 320Hz (audible on small speaker)
        float freq = 320.0f + 780.0f * std::exp(-t * 40.0f);
        float phase = 2.0f * 3.14159265f * freq * t;
        float punchWave = std::sin(phase);

        // Noise transient crack
        float noise = dist(rng) * std::exp(-t * 55.0f);

        // Amplitude envelope
        float env = std::exp(-t * 22.0f);

        // Metallic harmonic snap (2400Hz)
        float snap = 0.5f * std::sin(2.0f * 3.14159265f * 2400.0f * t) * std::exp(-t * 60.0f);

        float val = (punchWave * 0.85f + noise * 0.75f + snap) * env * 1.35f;
        pcm[i] = std::clamp(val, -1.0f, 1.0f);
    }
    return pcm;
}

std::vector<float> DualSenseAudioSystem::GenerateReloadPcm(int sampleRate) {
    // Crisp mechanical trigger/magazine clicks
    int length = static_cast<int>(sampleRate * 0.26f);
    std::vector<float> pcm(length, 0.0f);

    auto AddClick = [&](float startSec, float freq) {
        int startSample = static_cast<int>(startSec * sampleRate);
        int clickLen = static_cast<int>(sampleRate * 0.035f);
        for (int i = 0; i < clickLen && (startSample + i) < length; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            float env = (1.0f - t / 0.035f) * std::exp(-t * 80.0f);
            float wave = std::sin(2.0f * 3.14159265f * freq * t);
            pcm[startSample + i] += wave * env * 1.1f;
        }
    };

    AddClick(0.01f, 2400.0f);
    AddClick(0.08f, 3200.0f);
    AddClick(0.16f, 1800.0f);

    return pcm;
}

std::vector<float> DualSenseAudioSystem::GenerateImpactPcm(int sampleRate) {
    // Heavy explosive boom with mid-frequency crunch
    int length = static_cast<int>(sampleRate * 0.32f);
    std::vector<float> pcm(length);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    for (int i = 0; i < length; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float freq = 280.0f + 550.0f * std::exp(-t * 25.0f);
        float wave = std::sin(2.0f * 3.14159265f * freq * t);
        float noise = dist(rng) * std::exp(-t * 30.0f) * 0.4f;
        float env = std::exp(-t * 14.0f);
        pcm[i] = std::clamp((wave * 0.85f + noise) * env * 1.3f, -1.0f, 1.0f);
    }
    return pcm;
}

std::vector<float> DualSenseAudioSystem::GenerateChimePcm(float freq, float duration, int sampleRate) {
    int length = static_cast<int>(sampleRate * duration);
    std::vector<float> pcm(length);

    for (int i = 0; i < length; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float wave1 = std::sin(2.0f * 3.14159265f * freq * t);
        float wave2 = 0.4f * std::sin(2.0f * 3.14159265f * (freq * 2.0f) * t);
        float env = std::exp(-t * (4.5f / duration));
        pcm[i] = (wave1 + wave2) * env * 0.85f;
    }
    return pcm;
}

std::vector<float> DualSenseAudioSystem::GenerateRainDropPcm(int sampleRate) {
    int length = static_cast<int>(sampleRate * 0.025f);
    std::vector<float> pcm(length);

    for (int i = 0; i < length; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float freq = 1200.0f + 800.0f * std::exp(-t * 150.0f);
        float wave = std::sin(2.0f * 3.14159265f * freq * t);
        float env = std::exp(-t * 160.0f);
        pcm[i] = wave * env * 0.75f;
    }
    return pcm;
}

std::vector<float> DualSenseAudioSystem::GenerateTonePcm(float freq, float duration, int sampleRate) {
    int length = static_cast<int>(sampleRate * duration);
    std::vector<float> pcm(length);

    for (int i = 0; i < length; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        pcm[i] = std::sin(2.0f * 3.14159265f * freq * t) * 0.5f;
    }
    return pcm;
}

void DualSenseAudioSystem::PlayShootSound(float volume, float pitch) {
    int curRate = (m_sampleRate > 0) ? m_sampleRate : 48000;
    ActiveSound sound;
    sound.pcmSamples = GenerateShootPcm(curRate);
    sound.sampleRate = curRate;
    sound.channels = 1;
    sound.subPlayhead = 0.0f;
    sound.volume = volume;
    sound.pitch = pitch;

    std::lock_guard<std::mutex> lock(m_soundMutex);
    m_activeSounds.push_back(std::move(sound));
}

void DualSenseAudioSystem::PlayReloadSound(float volume) {
    int curRate = (m_sampleRate > 0) ? m_sampleRate : 48000;
    ActiveSound sound;
    sound.pcmSamples = GenerateReloadPcm(curRate);
    sound.sampleRate = curRate;
    sound.channels = 1;
    sound.subPlayhead = 0.0f;
    sound.volume = volume;

    std::lock_guard<std::mutex> lock(m_soundMutex);
    m_activeSounds.push_back(std::move(sound));
}

void DualSenseAudioSystem::PlayImpactSound(float volume) {
    int curRate = (m_sampleRate > 0) ? m_sampleRate : 48000;
    ActiveSound sound;
    sound.pcmSamples = GenerateImpactPcm(curRate);
    sound.sampleRate = curRate;
    sound.channels = 1;
    sound.subPlayhead = 0.0f;
    sound.volume = volume;

    std::lock_guard<std::mutex> lock(m_soundMutex);
    m_activeSounds.push_back(std::move(sound));
}

void DualSenseAudioSystem::PlayChimeSound(float freq, float duration, float volume) {
    int curRate = (m_sampleRate > 0) ? m_sampleRate : 48000;
    ActiveSound sound;
    sound.pcmSamples = GenerateChimePcm(freq, duration, curRate);
    sound.sampleRate = curRate;
    sound.channels = 1;
    sound.subPlayhead = 0.0f;
    sound.volume = volume;

    std::lock_guard<std::mutex> lock(m_soundMutex);
    m_activeSounds.push_back(std::move(sound));
}

void DualSenseAudioSystem::PlayRainDropSound(float volume) {
    int curRate = (m_sampleRate > 0) ? m_sampleRate : 48000;
    ActiveSound sound;
    sound.pcmSamples = GenerateRainDropPcm(curRate);
    sound.sampleRate = curRate;
    sound.channels = 1;
    sound.subPlayhead = 0.0f;
    sound.volume = volume;

    std::lock_guard<std::mutex> lock(m_soundMutex);
    m_activeSounds.push_back(std::move(sound));
}

void DualSenseAudioSystem::PlayTone(float frequency, float durationSeconds, float volume) {
    int curRate = (m_sampleRate > 0) ? m_sampleRate : 48000;
    ActiveSound sound;
    sound.pcmSamples = GenerateTonePcm(frequency, durationSeconds, curRate);
    sound.sampleRate = curRate;
    sound.channels = 1;
    sound.subPlayhead = 0.0f;
    sound.volume = volume;

    std::lock_guard<std::mutex> lock(m_soundMutex);
    m_activeSounds.push_back(std::move(sound));
}

bool DualSenseAudioSystem::PlayWavFile(const std::string& filepath, float volume, bool loop) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    file.seekg(0, std::ios::end);
    size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);

    return PlayWavMemory(buffer.data(), size, volume, loop);
}

bool DualSenseAudioSystem::PlayWavMemory(const uint8_t* wavData, size_t dataSize, float volume, bool loop) {
    if (!wavData || dataSize < 44) return false;

    // RIFF Header verification
    if (memcmp(wavData, "RIFF", 4) != 0 || memcmp(wavData + 8, "WAVE", 4) != 0) {
        return false;
    }

    size_t offset = 12;
    uint16_t audioFormat = 1; // PCM = 1, IEEE Float = 3
    uint16_t numChannels = 2;
    uint32_t srcSampleRate = 44100;
    uint16_t bitsPerSample = 16;
    const uint8_t* pcmDataPtr = nullptr;
    uint32_t pcmDataBytes = 0;

    while (offset + 8 <= dataSize) {
        char chunkId[5] = {};
        memcpy(chunkId, wavData + offset, 4);
        uint32_t chunkSize = *reinterpret_cast<const uint32_t*>(wavData + offset + 4);

        if (strcmp(chunkId, "fmt ") == 0 && chunkSize >= 16) {
            audioFormat = *reinterpret_cast<const uint16_t*>(wavData + offset + 8);
            numChannels = *reinterpret_cast<const uint16_t*>(wavData + offset + 10);
            srcSampleRate = *reinterpret_cast<const uint32_t*>(wavData + offset + 12);
            bitsPerSample = *reinterpret_cast<const uint16_t*>(wavData + offset + 22);
        } else if (strcmp(chunkId, "data") == 0) {
            pcmDataPtr = wavData + offset + 8;
            pcmDataBytes = (std::min)(chunkSize, static_cast<uint32_t>(dataSize - (offset + 8)));
            break;
        }

        offset += 8 + ((chunkSize + 1) & ~1);
    }

    if (!pcmDataPtr || pcmDataBytes == 0 || numChannels == 0) return false;

    size_t numSamples = pcmDataBytes / (bitsPerSample / 8);
    std::vector<float> rawFloatPcm(numSamples);

    if (audioFormat == 1) { // Integer PCM
        if (bitsPerSample == 16) {
            const int16_t* p16 = reinterpret_cast<const int16_t*>(pcmDataPtr);
            for (size_t i = 0; i < numSamples; ++i) {
                rawFloatPcm[i] = p16[i] / 32768.0f;
            }
        } else if (bitsPerSample == 8) {
            for (size_t i = 0; i < numSamples; ++i) {
                rawFloatPcm[i] = (pcmDataPtr[i] - 128.0f) / 128.0f;
            }
        } else if (bitsPerSample == 24) {
            for (size_t i = 0; i < numSamples; ++i) {
                int32_t val = (pcmDataPtr[i * 3 + 0] << 8) | (pcmDataPtr[i * 3 + 1] << 16) | (pcmDataPtr[i * 3 + 2] << 24);
                rawFloatPcm[i] = val / 2147483648.0f;
            }
        } else if (bitsPerSample == 32) {
            const int32_t* p32 = reinterpret_cast<const int32_t*>(pcmDataPtr);
            for (size_t i = 0; i < numSamples; ++i) {
                rawFloatPcm[i] = p32[i] / 2147483648.0f;
            }
        }
    } else if (audioFormat == 3 && bitsPerSample == 32) { // IEEE Float
        const float* pf = reinterpret_cast<const float*>(pcmDataPtr);
        for (size_t i = 0; i < numSamples; ++i) {
            rawFloatPcm[i] = pf[i];
        }
    } else {
        return false;
    }

    ActiveSound sound;
    sound.channels = numChannels;
    sound.sampleRate = srcSampleRate;
    sound.volume = volume;
    sound.loop = loop;
    sound.subPlayhead = 0.0f;
    sound.pcmSamples = std::move(rawFloatPcm);

    std::lock_guard<std::mutex> lock(m_soundMutex);
    m_activeSounds.push_back(std::move(sound));
    return true;
}

void DualSenseAudioSystem::PlayMelodyDemo() {
    StopAllSounds();

    // Generate a 2.5-second musical fanfare / arpeggio (C-E-G-C-G-E-C) for speaker testing
    int sr = (m_sampleRate > 0) ? m_sampleRate : 48000;
    float noteDur = 0.28f;
    size_t noteFrames = static_cast<size_t>(sr * noteDur);
    float notes[] = { 523.25f, 659.25f, 783.99f, 1046.50f, 783.99f, 659.25f, 523.25f, 659.25f, 1046.50f };
    size_t numNotes = sizeof(notes) / sizeof(notes[0]);

    std::vector<float> pcm;
    pcm.reserve(noteFrames * numNotes);

    for (size_t n = 0; n < numNotes; ++n) {
        float f = notes[n];
        for (size_t i = 0; i < noteFrames; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sr);
            // Envelope: fast attack, exponential decay
            float env = std::exp(-4.5f * (static_cast<float>(i) / static_cast<float>(noteFrames)));
            // Fundamental + 2nd harmonic (warm bell/chime timbre)
            float s = 0.75f * std::sin(2.0f * 3.14159265f * f * t) + 0.25f * std::sin(4.0f * 3.14159265f * f * t);
            pcm.push_back(s * env * 0.95f);
        }
    }

    ActiveSound sound;
    sound.channels = 1;
    sound.sampleRate = sr;
    sound.volume = 1.0f;
    sound.loop = false;
    sound.subPlayhead = 0.0f;
    sound.pcmSamples = std::move(pcm);

    std::lock_guard<std::mutex> lock(m_soundMutex);
    m_activeSounds.push_back(std::move(sound));
}

bool DualSenseAudioSystem::IsPlayingSound() const {
    return !m_activeSounds.empty();
}

void DualSenseAudioSystem::StopAllSounds() {
    std::lock_guard<std::mutex> lock(m_soundMutex);
    m_activeSounds.clear();
}

} // namespace EngineAudio

#elif defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)

#include "PS4Audio.h"
#include <cmath>

namespace EngineAudio {

DualSenseAudioSystem& DualSenseAudioSystem::Get() {
    static DualSenseAudioSystem s_instance;
    return s_instance;
}

DualSenseAudioSystem::DualSenseAudioSystem() = default;
DualSenseAudioSystem::~DualSenseAudioSystem() = default;

bool DualSenseAudioSystem::Initialize() {
    return PS4Audio::Get().Initialize();
}

void DualSenseAudioSystem::Shutdown() {
    PS4Audio::Get().Shutdown();
}

void DualSenseAudioSystem::RefreshAudioDevices() {}
bool DualSenseAudioSystem::SetSelectedDevice(int) { return true; }
bool DualSenseAudioSystem::SelectDefaultDevice() { return true; }
bool DualSenseAudioSystem::SelectBluetoothDevice() { return false; }
bool DualSenseAudioSystem::IsCurrentDeviceBluetooth() const { return false; }
void DualSenseAudioSystem::SetBluetoothBufferDurationMs(int) {}

void DualSenseAudioSystem::PlayShootSound(float volume, float pitch) {
    constexpr size_t count = 2048;
    int16_t buffer[count * 2];
    for (size_t i = 0; i < count; ++i) {
        float t = static_cast<float>(i) / 48000.0f;
        float decay = 1.0f - (static_cast<float>(i) / count);
        float sample = std::sin(2.0f * 3.14159f * 800.0f * pitch * t) * decay * volume;
        int16_t s16 = static_cast<int16_t>(sample * 16000.0f);
        buffer[i * 2] = s16;
        buffer[i * 2 + 1] = s16;
    }
    PS4Audio::Get().QueueAudio(buffer, count * 2);
}

void DualSenseAudioSystem::PlayReloadSound(float volume) {
    constexpr size_t count = 4096;
    int16_t buffer[count * 2];
    for (size_t i = 0; i < count; ++i) {
        float t = static_cast<float>(i) / 48000.0f;
        float decay = 1.0f - (static_cast<float>(i) / count);
        float sample = std::sin(2.0f * 3.14159f * 400.0f * t) * decay * volume;
        int16_t s16 = static_cast<int16_t>(sample * 14000.0f);
        buffer[i * 2] = s16;
        buffer[i * 2 + 1] = s16;
    }
    PS4Audio::Get().QueueAudio(buffer, count * 2);
}

void DualSenseAudioSystem::PlayImpactSound(float volume) {
    PlayShootSound(volume, 0.5f);
}

void DualSenseAudioSystem::PlayChimeSound(float freq, float duration, float volume) {
    PlayTone(freq, duration, volume);
}

void DualSenseAudioSystem::PlayRainDropSound(float volume) {
    PlayShootSound(volume * 0.3f, 2.0f);
}

void DualSenseAudioSystem::PlayTone(float frequency, float durationSeconds, float volume) {
    size_t count = static_cast<size_t>(48000.0f * durationSeconds);
    if (count > 48000 * 2) count = 48000 * 2;
    std::vector<int16_t> buffer(count * 2);
    for (size_t i = 0; i < count; ++i) {
        float t = static_cast<float>(i) / 48000.0f;
        float sample = std::sin(2.0f * 3.14159f * frequency * t) * volume;
        int16_t s16 = static_cast<int16_t>(sample * 16000.0f);
        buffer[i * 2] = s16;
        buffer[i * 2 + 1] = s16;
    }
    PS4Audio::Get().QueueAudio(buffer.data(), buffer.size());
}

bool DualSenseAudioSystem::PlayWavFile(const std::string&, float, bool) { return false; }
bool DualSenseAudioSystem::PlayWavMemory(const uint8_t*, size_t, float, bool) { return false; }
void DualSenseAudioSystem::PlayMelodyDemo() {}
bool DualSenseAudioSystem::IsPlayingSound() const { return false; }
void DualSenseAudioSystem::StopAllSounds() {}

} // namespace EngineAudio

#else

namespace EngineAudio {

DualSenseAudioSystem& DualSenseAudioSystem::Get() {
    static DualSenseAudioSystem s_instance;
    return s_instance;
}

DualSenseAudioSystem::DualSenseAudioSystem() = default;
DualSenseAudioSystem::~DualSenseAudioSystem() = default;

bool DualSenseAudioSystem::Initialize() { return true; }
void DualSenseAudioSystem::Shutdown() {}

void DualSenseAudioSystem::RefreshAudioDevices() {}
bool DualSenseAudioSystem::SetSelectedDevice(int) { return true; }
bool DualSenseAudioSystem::SelectDefaultDevice() { return true; }
bool DualSenseAudioSystem::SelectBluetoothDevice() { return false; }
bool DualSenseAudioSystem::IsCurrentDeviceBluetooth() const { return false; }
void DualSenseAudioSystem::SetBluetoothBufferDurationMs(int) {}

void DualSenseAudioSystem::PlayShootSound(float, float) {}
void DualSenseAudioSystem::PlayReloadSound(float) {}
void DualSenseAudioSystem::PlayImpactSound(float) {}
void DualSenseAudioSystem::PlayChimeSound(float, float, float) {}
void DualSenseAudioSystem::PlayRainDropSound(float) {}
void DualSenseAudioSystem::PlayTone(float, float, float) {}

bool DualSenseAudioSystem::PlayWavFile(const std::string&, float, bool) { return false; }
bool DualSenseAudioSystem::PlayWavMemory(const uint8_t*, size_t, float, bool) { return false; }
void DualSenseAudioSystem::PlayMelodyDemo() {}
bool DualSenseAudioSystem::IsPlayingSound() const { return false; }
void DualSenseAudioSystem::StopAllSounds() {}

} // namespace EngineAudio

#endif
