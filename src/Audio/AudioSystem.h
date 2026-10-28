#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <cstdint>

namespace EngineAudio {

enum class AudioBus {
    Master = 0,
    Music,
    SFX,
    Voice,
    Ambience,
    Count
};

struct SpatialSound {
    uint32_t id = 0;
    std::string name;
    AudioBus bus = AudioBus::SFX;
    bool is3D = false;
    float posX = 0.0f;
    float posY = 0.0f;
    float posZ = 0.0f;
    float volume = 1.0f;
    bool loop = false;
    void* internalSound = nullptr; // pointer to ma_sound
};

class AudioSystem {
public:
    static AudioSystem& Get();

    bool Initialize();
    void Shutdown();
    void Update(float dt);

    // Listener (attached to active camera)
    void UpdateListener(float posX, float posY, float posZ,
                        float forwardX, float forwardY, float forwardZ,
                        float upX = 0.0f, float upY = 1.0f, float upZ = 0.0f);

    // Playback API
    uint32_t Play2D(const std::string& filepath, AudioBus bus = AudioBus::SFX,
                    float volume = 1.0f, float pitch = 1.0f, bool loop = false);

    uint32_t Play3D(const std::string& filepath,
                    float posX, float posY, float posZ,
                    AudioBus bus = AudioBus::SFX,
                    float volume = 1.0f, float pitch = 1.0f,
                    float minDistance = 1.0f, float maxDistance = 50.0f,
                    bool loop = false);

    void StopSound(uint32_t soundId);
    void StopAll();
    void StopBus(AudioBus bus);

    void SetSoundPosition(uint32_t soundId, float x, float y, float z);
    void SetSoundVolume(uint32_t soundId, float volume);

    // Audio Bus Volume Controls [0.0f .. 2.0f]
    void SetBusVolume(AudioBus bus, float volume);
    float GetBusVolume(AudioBus bus) const;
    void SetMasterVolume(float volume) { SetBusVolume(AudioBus::Master, volume); }
    float GetMasterVolume() const { return GetBusVolume(AudioBus::Master); }

    // Procedural Sound Generator (immediate in-engine sound synthesis without external files)
    void PlayProceduralBeep(float frequency = 440.0f, float duration = 0.2f, float volume = 0.5f);
    void PlayProceduralCoin(float volume = 0.5f);
    void PlayProceduralStep(float volume = 0.4f, float pitch = 1.0f);
    void PlayProceduralExplosion(float volume = 0.7f);
    void PlayProcedural3DBeacon(float x, float y, float z, float volume = 0.6f);

    // Status & Diagnostics
    bool IsInitialized() const { return m_initialized; }
    size_t GetActiveSoundCount();
    const char* GetBackendName() const;

    // ImGui Editor UI
    void DrawUI();
    bool IsUIOpen() const { return m_uiOpen; }
    void SetUIOpen(bool open) { m_uiOpen = open; }
    void ToggleUI() { m_uiOpen = !m_uiOpen; }

    struct Impl;

private:
    AudioSystem();
    ~AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;
    std::unique_ptr<Impl> m_impl;

    bool m_initialized = false;
    bool m_uiOpen = false;
    float m_busVolumes[static_cast<size_t>(AudioBus::Count)];
    std::mutex m_mutex;
    uint32_t m_nextSoundId = 1;
};

} // namespace EngineAudio
