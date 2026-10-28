#pragma once

#if defined(_WIN32)
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#else
#include "Platform/PlatformTypes.h"
#endif
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>

namespace EngineAudio {

struct AudioDeviceDesc {
    std::wstring id;
    std::string name;
    bool isDualSense = false;
    bool isBluetooth = false;
    bool isDefault = false;
    std::string busType = "Audio";
};

enum class DualSenseChannelRouting {
    Auto,       // Channels 3-4 if 4-channel DualSense USB, else Channels 1-2
    StereoMain, // Channels 1-2 (Headphones/Main)
    SpeakerOnly,// Channels 3-4 (DualSense Internal Speaker on 4ch USB)
    AllChannels // Broadcast to all channels
};

struct ActiveSound {
    std::vector<float> pcmSamples; // Mono or Stereo normalized float samples [-1.0, 1.0]
    int sampleRate = 48000;
    int channels = 1;
    float subPlayhead = 0.0f;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool isFinished = false;
};

class DualSenseAudioSystem {
public:
    static DualSenseAudioSystem& Get();

    DualSenseAudioSystem();
    ~DualSenseAudioSystem();

    bool Initialize();
    void Shutdown();

    // Device Management
    void RefreshAudioDevices();
    const std::vector<AudioDeviceDesc>& GetAudioDevices() const { return m_devices; }
    int GetSelectedDeviceIndex() const { return m_selectedDeviceIndex; }
    bool SetSelectedDevice(int index);
    bool SelectDefaultDevice();
    bool IsDeviceOpen() const { return m_audioClient != nullptr; }
    const std::string& GetCurrentDeviceName() const { return m_currentDeviceName; }
    bool IsDualSenseDevice() const { return m_isDualSenseDevice; }
    bool IsFallbackSpeaker() const { return !m_isDualSenseDevice && m_audioClient != nullptr; }
    int GetDeviceSampleRate() const { return m_sampleRate; }
    int GetDeviceChannels() const { return m_deviceChannels; }

    // Volume & Configuration
    void SetMasterVolume(float volume) { m_masterVolume = (volume < 0.0f) ? 0.0f : (volume > 2.0f ? 2.0f : volume); }
    float GetMasterVolume() const { return m_masterVolume; }
    void SetChannelRouting(DualSenseChannelRouting routing) { m_channelRouting = routing; }
    DualSenseChannelRouting GetChannelRouting() const { return m_channelRouting; }
    void SetGameplaySoundsEnabled(bool enabled) { m_gameplaySoundsEnabled = enabled; }
    bool IsGameplaySoundsEnabled() const { return m_gameplaySoundsEnabled; }
    // Bluetooth Audio Management
    bool SelectBluetoothDevice();
    bool IsCurrentDeviceBluetooth() const;
    void SetBluetoothAudioEnabled(bool enabled) { m_bluetoothAudioEnabled = enabled; }
    bool IsBluetoothAudioEnabled() const { return m_bluetoothAudioEnabled; }
    void SetBluetoothSpeakerSimulation(bool enabled) { m_bluetoothSpeakerSim = enabled; }
    bool IsBluetoothSpeakerSimulation() const { return m_bluetoothSpeakerSim; }
    void SetBluetoothBufferDurationMs(int ms);
    int GetBluetoothBufferDurationMs() const { return m_bluetoothBufferMs; }

    // Audio Energy Telemetry (for Audio-to-Haptics and Audio-Reactive Lightbar)
    float GetAudioEnergy() const { return m_audioEnergy.load(); }
    float GetBassAudioEnergy() const { return m_bassEnergy.load(); }
    float GetTrebleAudioEnergy() const { return m_trebleEnergy.load(); }

    // Procedural Synthesized Sound Effects for DualSense Speaker
    void PlayShootSound(float volume = 1.0f, float pitch = 1.0f);
    void PlayReloadSound(float volume = 1.0f);
    void PlayImpactSound(float volume = 1.0f);
    void PlayChimeSound(float freq = 880.0f, float duration = 0.15f, float volume = 1.0f);
    void PlayRainDropSound(float volume = 0.5f);
    void PlayTone(float frequency, float durationSeconds, float volume = 0.5f);

    // Custom WAV Audio File Playback & Demo Tone Melody
    bool PlayWavFile(const std::string& filepath, float volume = 1.0f, bool loop = false);
    bool PlayWavMemory(const uint8_t* wavData, size_t dataSize, float volume = 1.0f, bool loop = false);
    void PlayMelodyDemo();
    bool IsPlayingSound() const;
    void StopAllSounds();

private:
    bool OpenAudioDevice(const std::wstring& deviceId);
    void CloseAudioDevice();
    void AudioThread();
    void MixAudio(float* outputBuffer, size_t frameCount);

    static std::vector<float> GenerateShootPcm(int sampleRate);
    static std::vector<float> GenerateReloadPcm(int sampleRate);
    static std::vector<float> GenerateImpactPcm(int sampleRate);
    static std::vector<float> GenerateChimePcm(float freq, float duration, int sampleRate);
    static std::vector<float> GenerateRainDropPcm(int sampleRate);
    static std::vector<float> GenerateTonePcm(float freq, float duration, int sampleRate);

private:
    std::vector<AudioDeviceDesc> m_devices;
    int m_selectedDeviceIndex = -1;
    std::string m_currentDeviceName;
    bool m_isDualSenseDevice = false;
    bool m_isBluetoothDevice = false;

    // COM & WASAPI Interfaces
    IMMDeviceEnumerator* m_deviceEnumerator = nullptr;
    IMMDevice* m_audioDevice = nullptr;
    IAudioClient* m_audioClient = nullptr;
    IAudioRenderClient* m_renderClient = nullptr;

    WAVEFORMATEXTENSIBLE m_waveFormat = {};
    int m_sampleRate = 48000;
    int m_deviceChannels = 2;
    int m_bitsPerSample = 32;
    bool m_isFloatFormat = true;
    UINT32 m_bufferFrameCount = 0;

    // Audio Streaming Thread
    std::atomic<bool> m_running{ false };
    std::thread m_audioThread;
    HANDLE m_audioEvent = nullptr;

    // Mixing & Sound state
    std::mutex m_soundMutex;
    std::vector<ActiveSound> m_activeSounds;
    float m_masterVolume = 1.0f;
    DualSenseChannelRouting m_channelRouting = DualSenseChannelRouting::Auto;
    bool m_gameplaySoundsEnabled = true;
    bool m_bluetoothAudioEnabled = true;
    bool m_bluetoothSpeakerSim = true;
    int m_bluetoothBufferMs = 80;
    bool m_comInitialized = false;
    std::atomic<float> m_audioEnergy{ 0.0f };
    std::atomic<float> m_bassEnergy{ 0.0f };
    std::atomic<float> m_trebleEnergy{ 0.0f };
};

} // namespace EngineAudio
