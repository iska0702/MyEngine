#pragma once

#include <cstdint>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

namespace EngineAudio {

class PS4Audio {
public:
    static PS4Audio& Get();

    bool Initialize(int sampleRate = 48000, int channels = 2);
    void Shutdown();

    // Stream PCM audio buffer (16-bit signed stereo)
    void QueueAudio(const int16_t* samples, size_t sampleCount);

    void SetMasterVolume(float vol) { m_masterVolume = vol; }
    float GetMasterVolume() const { return m_masterVolume; }

    bool IsRunning() const { return m_running; }

private:
    PS4Audio();
    ~PS4Audio();

    void AudioThreadFunc();

    int m_audioHandle = -1;
    int m_sampleRate = 48000;
    int m_channels = 2;
    float m_masterVolume = 1.0f;

    std::atomic<bool> m_running{ false };
    std::thread m_audioThread;
    std::mutex m_bufferMutex;
    std::vector<int16_t> m_pendingSamples;
};

} // namespace EngineAudio
