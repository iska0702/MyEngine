#include "PS4Audio.h"
#include <iostream>
#include <cstring>
#include <chrono>

#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
#include <orbis/libkernel.h>
#include <orbis/AudioOut.h>
#include <orbis/UserService.h>
#include <orbis/Sysmodule.h>
#endif

namespace EngineAudio {

PS4Audio& PS4Audio::Get() {
    static PS4Audio s_instance;
    return s_instance;
}

PS4Audio::PS4Audio() = default;

PS4Audio::~PS4Audio() {
    Shutdown();
}

bool PS4Audio::Initialize(int sampleRate, int channels) {
    if (m_running) return true;

    m_sampleRate = sampleRate;
    m_channels = channels;

#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    const uint32_t grain = 1024;
    m_audioHandle = sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM,
                                    ORBIS_AUDIO_OUT_PORT_TYPE_MAIN,
                                    0, grain, m_sampleRate,
                                    ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    if (m_audioHandle < 0) {
        std::cerr << "[PS4Audio] sceAudioOutOpen failed: " << m_audioHandle << std::endl;
        return false;
    }
#endif

    m_running = true;
    m_audioThread = std::thread(&PS4Audio::AudioThreadFunc, this);
    std::cout << "[PS4Audio] Audio engine initialized (48kHz Stereo)" << std::endl;
    return true;
}

void PS4Audio::Shutdown() {
    if (!m_running) return;

    m_running = false;
    if (m_audioThread.joinable()) {
        m_audioThread.join();
    }

#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    if (m_audioHandle >= 0) {
        sceAudioOutClose(m_audioHandle);
        m_audioHandle = -1;
    }
#endif

    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_pendingSamples.clear();
}

void PS4Audio::QueueAudio(const int16_t* samples, size_t sampleCount) {
    if (!samples || sampleCount == 0 || !m_running) return;

    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_pendingSamples.insert(m_pendingSamples.end(), samples, samples + sampleCount);
}

void PS4Audio::AudioThreadFunc() {
    constexpr size_t GRAIN = 1024;
    constexpr size_t CHANNELS = 2;
    int16_t silenceBuffer[GRAIN * CHANNELS] = {};
    int16_t outputBuffer[GRAIN * CHANNELS] = {};

    while (m_running) {
        bool hasData = false;
        {
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            if (m_pendingSamples.size() >= GRAIN * CHANNELS) {
                hasData = true;
                std::memcpy(outputBuffer, m_pendingSamples.data(), GRAIN * CHANNELS * sizeof(int16_t));
                m_pendingSamples.erase(m_pendingSamples.begin(), m_pendingSamples.begin() + GRAIN * CHANNELS);
            }
        }

        const int16_t* ptr = hasData ? outputBuffer : silenceBuffer;

#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
        if (m_audioHandle >= 0) {
            sceAudioOutOutput(m_audioHandle, ptr);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
#endif
    }
}

} // namespace EngineAudio
