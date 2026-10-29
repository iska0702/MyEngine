#define MINIAUDIO_IMPLEMENTATION
#include "ThirdParty/Libraries/EngineDependencies/miniaudio.h"

#include "AudioSystem.h"
#include "Storage/ContentFile.h"
#include "Platform/PlatformUtils.h"
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER) && !defined(ENGINE_PLATFORM_PS4)
#include "imgui/imgui.h"
#endif

#include <iostream>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace EngineAudio {

struct SoundInstance {
    uint32_t id = 0;
    std::string name;
    AudioBus bus = AudioBus::SFX;
    ma_sound sound;
    bool is3D = false;
    bool loop = false;
    bool active = false;
    std::vector<uint8_t> encodedData;
    ma_decoder decoder {};
    bool ownsDecoder = false;
    ~SoundInstance() { if (ownsDecoder) ma_decoder_uninit(&decoder); }
};

struct AudioSystem::Impl {
    ma_engine engine;
    ma_sound_group musicGroup;
    ma_sound_group sfxGroup;
    ma_sound_group voiceGroup;
    ma_sound_group ambienceGroup;

    std::unordered_map<uint32_t, std::unique_ptr<SoundInstance>> activeSounds;
};

AudioSystem& AudioSystem::Get() {
    static AudioSystem s_instance;
    return s_instance;
}

AudioSystem::AudioSystem() {
    for (size_t i = 0; i < static_cast<size_t>(AudioBus::Count); ++i) {
        m_busVolumes[i] = 1.0f;
    }
}

AudioSystem::~AudioSystem() {
    Shutdown();
}

bool AudioSystem::Initialize() {
    if (m_initialized) return true;

    m_impl = std::make_unique<Impl>();

    ma_engine_config config = ma_engine_config_init();
    config.channels = 2;
    config.sampleRate = 48000;

    ma_result result = ma_engine_init(&config, &m_impl->engine);
    if (result != MA_SUCCESS) {
        std::cerr << "[AudioSystem] Failed to initialize miniaudio engine: " << result << std::endl;
        m_impl.reset();
        return false;
    }

    // Initialize Audio Buses (Sound Groups)
    ma_sound_group_init(&m_impl->engine, 0, NULL, &m_impl->musicGroup);
    ma_sound_group_init(&m_impl->engine, 0, NULL, &m_impl->sfxGroup);
    ma_sound_group_init(&m_impl->engine, 0, NULL, &m_impl->voiceGroup);
    ma_sound_group_init(&m_impl->engine, 0, NULL, &m_impl->ambienceGroup);

    // Apply initial bus volumes
    for (size_t i = 0; i < static_cast<size_t>(AudioBus::Count); ++i) {
        SetBusVolume(static_cast<AudioBus>(i), m_busVolumes[i]);
    }

    // Setup initial listener
    ma_engine_listener_set_position(&m_impl->engine, 0, 0.0f, 1.5f, 0.0f);
    ma_engine_listener_set_direction(&m_impl->engine, 0, 0.0f, 0.0f, 1.0f);
    ma_engine_listener_set_world_up(&m_impl->engine, 0, 0.0f, 1.0f, 0.0f);

    m_initialized = true;
    std::cout << "[AudioSystem] Initialized successfully. Backend: " << GetBackendName() << std::endl;
    return true;
}

void AudioSystem::Shutdown() {
    if (!m_initialized || !m_impl) return;

    StopAll();

    ma_sound_group_uninit(&m_impl->musicGroup);
    ma_sound_group_uninit(&m_impl->sfxGroup);
    ma_sound_group_uninit(&m_impl->voiceGroup);
    ma_sound_group_uninit(&m_impl->ambienceGroup);

    ma_engine_uninit(&m_impl->engine);
    m_impl.reset();
    m_initialized = false;
}

const char* AudioSystem::GetBackendName() const {
    if (!m_initialized || !m_impl) return "None";
    ma_device* pDevice = ma_engine_get_device(&m_impl->engine);
    if (!pDevice || !pDevice->pContext) return "Unknown";
    return ma_get_backend_name(pDevice->pContext->backend);
}

void AudioSystem::UpdateListener(float posX, float posY, float posZ,
                                 float forwardX, float forwardY, float forwardZ,
                                 float upX, float upY, float upZ) {
    if (!m_initialized || !m_impl) return;

    ma_engine_listener_set_position(&m_impl->engine, 0, posX, posY, posZ);
    ma_engine_listener_set_direction(&m_impl->engine, 0, forwardX, forwardY, forwardZ);
    ma_engine_listener_set_world_up(&m_impl->engine, 0, upX, upY, upZ);
}

void AudioSystem::SetBusVolume(AudioBus bus, float volume) {
    size_t idx = static_cast<size_t>(bus);
    if (idx >= static_cast<size_t>(AudioBus::Count)) return;

    volume = std::clamp(volume, 0.0f, 2.0f);
    m_busVolumes[idx] = volume;

    if (!m_initialized || !m_impl) return;

    switch (bus) {
        case AudioBus::Master:
            ma_engine_set_volume(&m_impl->engine, volume);
            break;
        case AudioBus::Music:
            ma_sound_group_set_volume(&m_impl->musicGroup, volume);
            break;
        case AudioBus::SFX:
            ma_sound_group_set_volume(&m_impl->sfxGroup, volume);
            break;
        case AudioBus::Voice:
            ma_sound_group_set_volume(&m_impl->voiceGroup, volume);
            break;
        case AudioBus::Ambience:
            ma_sound_group_set_volume(&m_impl->ambienceGroup, volume);
            break;
        default:
            break;
    }
}

float AudioSystem::GetBusVolume(AudioBus bus) const {
    size_t idx = static_cast<size_t>(bus);
    if (idx >= static_cast<size_t>(AudioBus::Count)) return 1.0f;
    return m_busVolumes[idx];
}

static ma_sound_group* GetSoundGroup(AudioSystem::Impl* impl, AudioBus bus) {
    switch (bus) {
        case AudioBus::Music:    return &impl->musicGroup;
        case AudioBus::SFX:      return &impl->sfxGroup;
        case AudioBus::Voice:    return &impl->voiceGroup;
        case AudioBus::Ambience: return &impl->ambienceGroup;
        default:                 return nullptr;
    }
}

static ma_result InitContentSound(AudioSystem::Impl* impl, SoundInstance& instance,
                                  const std::string& path, ma_sound_group* group) {
    if (!EngineStorage::ReadContentBytes(path, instance.encodedData) || instance.encodedData.empty())
        return MA_INVALID_FILE;
    auto config = ma_decoder_config_init(ma_format_f32, 0, ma_engine_get_sample_rate(&impl->engine));
    auto result = ma_decoder_init_memory(instance.encodedData.data(), instance.encodedData.size(), &config, &instance.decoder);
    if (result != MA_SUCCESS) return result;
    instance.ownsDecoder = true;
    return ma_sound_init_from_data_source(&impl->engine, &instance.decoder, 0, group, &instance.sound);
}

uint32_t AudioSystem::Play2D(const std::string& filepath, AudioBus bus, float volume, float pitch, bool loop) {
    if (!m_initialized || !m_impl) return 0;

    std::lock_guard<std::mutex> lock(m_mutex);

    auto instance = std::make_unique<SoundInstance>();
    uint32_t id = m_nextSoundId++;
    instance->id = id;
    instance->name = std::filesystem::path(filepath).filename().string();
    instance->bus = bus;
    instance->is3D = false;
    instance->loop = loop;
    instance->active = true;

    ma_sound_group* pGroup = GetSoundGroup(m_impl.get(), bus);
    ma_result res = InitContentSound(m_impl.get(), *instance, filepath, pGroup);
    if (res != MA_SUCCESS) {
        return 0;
    }

    ma_sound_set_spatialization_enabled(&instance->sound, MA_FALSE);
    ma_sound_set_volume(&instance->sound, volume);
    ma_sound_set_pitch(&instance->sound, pitch);
    ma_sound_set_looping(&instance->sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_start(&instance->sound);

    m_impl->activeSounds[id] = std::move(instance);
    return id;
}

uint32_t AudioSystem::Play3D(const std::string& filepath, float posX, float posY, float posZ,
                             AudioBus bus, float volume, float pitch,
                             float minDistance, float maxDistance, bool loop) {
    if (!m_initialized || !m_impl) return 0;

    std::lock_guard<std::mutex> lock(m_mutex);

    auto instance = std::make_unique<SoundInstance>();
    uint32_t id = m_nextSoundId++;
    instance->id = id;
    instance->name = std::filesystem::path(filepath).filename().string();
    instance->bus = bus;
    instance->is3D = true;
    instance->loop = loop;
    instance->active = true;

    ma_sound_group* pGroup = GetSoundGroup(m_impl.get(), bus);
    ma_result res = InitContentSound(m_impl.get(), *instance, filepath, pGroup);
    if (res != MA_SUCCESS) {
        return 0;
    }

    ma_sound_set_spatialization_enabled(&instance->sound, MA_TRUE);
    ma_sound_set_position(&instance->sound, posX, posY, posZ);
    ma_sound_set_min_distance(&instance->sound, minDistance);
    ma_sound_set_max_distance(&instance->sound, maxDistance);
    ma_sound_set_attenuation_model(&instance->sound, ma_attenuation_model_inverse);
    ma_sound_set_volume(&instance->sound, volume);
    ma_sound_set_pitch(&instance->sound, pitch);
    ma_sound_set_looping(&instance->sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_start(&instance->sound);

    m_impl->activeSounds[id] = std::move(instance);
    return id;
}

void AudioSystem::StopSound(uint32_t soundId) {
    if (!m_initialized || !m_impl) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_impl->activeSounds.find(soundId);
    if (it != m_impl->activeSounds.end()) {
        ma_sound_stop(&it->second->sound);
        ma_sound_uninit(&it->second->sound);
        m_impl->activeSounds.erase(it);
    }
}

void AudioSystem::StopAll() {
    if (!m_initialized || !m_impl) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_impl->activeSounds) {
        ma_sound_stop(&pair.second->sound);
        ma_sound_uninit(&pair.second->sound);
    }
    m_impl->activeSounds.clear();
}

void AudioSystem::StopBus(AudioBus bus) {
    if (!m_initialized || !m_impl) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_impl->activeSounds.begin(); it != m_impl->activeSounds.end();) {
        if (it->second->bus == bus) {
            ma_sound_stop(&it->second->sound);
            ma_sound_uninit(&it->second->sound);
            it = m_impl->activeSounds.erase(it);
        } else {
            ++it;
        }
    }
}

void AudioSystem::SetSoundPosition(uint32_t soundId, float x, float y, float z) {
    if (!m_initialized || !m_impl) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_impl->activeSounds.find(soundId);
    if (it != m_impl->activeSounds.end()) {
        ma_sound_set_position(&it->second->sound, x, y, z);
    }
}

void AudioSystem::SetSoundVolume(uint32_t soundId, float volume) {
    if (!m_initialized || !m_impl) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_impl->activeSounds.find(soundId);
    if (it != m_impl->activeSounds.end()) {
        ma_sound_set_volume(&it->second->sound, volume);
    }
}

size_t AudioSystem::GetActiveSoundCount() {
    if (!m_initialized || !m_impl) return 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_impl->activeSounds.size();
}

void AudioSystem::Update(float /*dt*/) {
    if (!m_initialized || !m_impl) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_impl->activeSounds.begin(); it != m_impl->activeSounds.end();) {
        if (!it->second->loop && ma_sound_at_end(&it->second->sound)) {
            ma_sound_uninit(&it->second->sound);
            it = m_impl->activeSounds.erase(it);
        } else {
            ++it;
        }
    }
}

// -----------------------------------------------------------------------------
// Procedural Waveform Synthesis (In-memory WAV generation)
// -----------------------------------------------------------------------------
static void WriteWav(const std::string& path, const std::vector<int16_t>& samples, uint32_t sampleRate = 44100) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return;

    uint32_t subChunk2Size = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    uint32_t chunkSize = 36 + subChunk2Size;

    // RIFF header
    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&chunkSize), 4);
    file.write("WAVE", 4);

    // fmt subchunk
    file.write("fmt ", 4);
    uint32_t subChunk1Size = 16;
    uint16_t audioFormat = 1; // PCM
    uint16_t numChannels = 1; // Mono
    uint32_t byteRate = sampleRate * 1 * sizeof(int16_t);
    uint16_t blockAlign = 1 * sizeof(int16_t);
    uint16_t bitsPerSample = 16;
    file.write(reinterpret_cast<const char*>(&subChunk1Size), 4);
    file.write(reinterpret_cast<const char*>(&audioFormat), 2);
    file.write(reinterpret_cast<const char*>(&numChannels), 2);
    file.write(reinterpret_cast<const char*>(&sampleRate), 4);
    file.write(reinterpret_cast<const char*>(&byteRate), 4);
    file.write(reinterpret_cast<const char*>(&blockAlign), 2);
    file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

    // data subchunk
    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&subChunk2Size), 4);
    file.write(reinterpret_cast<const char*>(samples.data()), subChunk2Size);
}

static std::string GetProceduralSoundPath(const std::string& name) {
    std::string dir = EnginePlatform::PlatformUtils::ResolveContentPath("Audio");
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        std::filesystem::create_directories(dir, ec);
    }
    return dir + "/" + name + ".wav";
}

void AudioSystem::PlayProceduralBeep(float frequency, float duration, float volume) {
    std::string path = GetProceduralSoundPath("procedural_beep");
    static bool generated = false;
    if (!generated || !std::filesystem::exists(path)) {
        uint32_t rate = 44100;
        size_t totalSamples = static_cast<size_t>(rate * duration);
        std::vector<int16_t> samples(totalSamples);
        for (size_t i = 0; i < totalSamples; ++i) {
            float t = static_cast<float>(i) / rate;
            float envelope = 1.0f - (static_cast<float>(i) / totalSamples); // Linear fade-out
            float val = std::sin(2.0f * 3.14159265f * frequency * t) * envelope;
            samples[i] = static_cast<int16_t>(val * 24000.0f);
        }
        WriteWav(path, samples, rate);
        generated = true;
    }
    Play2D(path, AudioBus::SFX, volume);
}

void AudioSystem::PlayProceduralCoin(float volume) {
    std::string path = GetProceduralSoundPath("procedural_coin");
    static bool generated = false;
    if (!generated || !std::filesystem::exists(path)) {
        uint32_t rate = 44100;
        float duration = 0.35f;
        size_t totalSamples = static_cast<size_t>(rate * duration);
        std::vector<int16_t> samples(totalSamples);
        for (size_t i = 0; i < totalSamples; ++i) {
            float t = static_cast<float>(i) / rate;
            float freq = (t < 0.12f) ? 987.77f : 1318.51f; // B5 -> E6
            float envelope = 1.0f - (t / duration);
            float val = std::sin(2.0f * 3.14159265f * freq * t) * envelope;
            samples[i] = static_cast<int16_t>(val * 26000.0f);
        }
        WriteWav(path, samples, rate);
        generated = true;
    }
    Play2D(path, AudioBus::SFX, volume);
}

void AudioSystem::PlayProceduralStep(float volume, float pitch) {
    std::string path = GetProceduralSoundPath("procedural_step");
    static bool generated = false;
    if (!generated || !std::filesystem::exists(path)) {
        uint32_t rate = 44100;
        float duration = 0.12f;
        size_t totalSamples = static_cast<size_t>(rate * duration);
        std::vector<int16_t> samples(totalSamples);
        for (size_t i = 0; i < totalSamples; ++i) {
            float t = static_cast<float>(i) / rate;
            float noise = ((rand() % 2000) - 1000) / 1000.0f;
            float lowFreq = std::sin(2.0f * 3.14159265f * 90.0f * t);
            float envelope = std::exp(-28.0f * t);
            float val = (noise * 0.4f + lowFreq * 0.6f) * envelope;
            samples[i] = static_cast<int16_t>(val * 28000.0f);
        }
        WriteWav(path, samples, rate);
        generated = true;
    }
    Play2D(path, AudioBus::SFX, volume, pitch);
}

void AudioSystem::PlayProceduralExplosion(float volume) {
    std::string path = GetProceduralSoundPath("procedural_explosion");
    static bool generated = false;
    if (!generated || !std::filesystem::exists(path)) {
        uint32_t rate = 44100;
        float duration = 1.2f;
        size_t totalSamples = static_cast<size_t>(rate * duration);
        std::vector<int16_t> samples(totalSamples);
        for (size_t i = 0; i < totalSamples; ++i) {
            float t = static_cast<float>(i) / rate;
            float noise = ((rand() % 2000) - 1000) / 1000.0f;
            float subBass = std::sin(2.0f * 3.14159265f * 45.0f * t);
            float envelope = std::exp(-3.5f * t);
            float val = (noise * 0.7f + subBass * 0.3f) * envelope;
            samples[i] = static_cast<int16_t>(val * 30000.0f);
        }
        WriteWav(path, samples, rate);
        generated = true;
    }
    Play2D(path, AudioBus::SFX, volume);
}

void AudioSystem::PlayProcedural3DBeacon(float x, float y, float z, float volume) {
    std::string path = GetProceduralSoundPath("procedural_beacon");
    static bool generated = false;
    if (!generated || !std::filesystem::exists(path)) {
        uint32_t rate = 44100;
        float duration = 0.5f;
        size_t totalSamples = static_cast<size_t>(rate * duration);
        std::vector<int16_t> samples(totalSamples);
        for (size_t i = 0; i < totalSamples; ++i) {
            float t = static_cast<float>(i) / rate;
            float freq = 600.0f + 200.0f * std::sin(12.0f * 3.14159265f * t);
            float envelope = 1.0f - (t / duration);
            float val = std::sin(2.0f * 3.14159265f * freq * t) * envelope;
            samples[i] = static_cast<int16_t>(val * 24000.0f);
        }
        WriteWav(path, samples, rate);
        generated = true;
    }
    Play3D(path, x, y, z, AudioBus::SFX, volume, 1.0f, 2.0f, 60.0f, false);
}

// -----------------------------------------------------------------------------
// ImGui Audio Studio Panel
// -----------------------------------------------------------------------------
void AudioSystem::DrawUI() {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER) && !defined(ENGINE_PLATFORM_PS4)
    if (!m_uiOpen) return;

    ImGui::SetNextWindowSize(ImVec2(520, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("3D Spatial Audio Studio", &m_uiOpen)) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "Engine Audio System (miniaudio)");
        ImGui::Text("Backend: %s | Active Voice Count: %zu", GetBackendName(), GetActiveSoundCount());
        ImGui::Separator();

        // Audio Bus Volume Sliders
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Audio Mixer & Buses:");
        const char* busNames[] = { "Master", "Music", "SFX", "Voice / Dialog", "Ambience" };
        for (int i = 0; i < 5; ++i) {
            AudioBus b = static_cast<AudioBus>(i);
            float vol = GetBusVolume(b);
            if (ImGui::SliderFloat(busNames[i], &vol, 0.0f, 1.5f, "%.2fx")) {
                SetBusVolume(b, vol);
            }
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Procedural Sound Synthesizer Test");
        ImGui::TextWrapped("Test instant sound playback through PC speakers/headphones without needing external files:");

        if (ImGui::Button("UI Beep (2D)")) {
            PlayProceduralBeep();
        }
        ImGui::SameLine();
        if (ImGui::Button("Coin Chime (2D)")) {
            PlayProceduralCoin();
        }
        ImGui::SameLine();
        if (ImGui::Button("Footstep (2D)")) {
            PlayProceduralStep();
        }
        ImGui::SameLine();
        if (ImGui::Button("Explosion (2D)")) {
            PlayProceduralExplosion();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("3D Spatial Audio Test");
        static float beaconPos[3] = { 5.0f, 1.5f, 5.0f };
        ImGui::DragFloat3("3D Sound Position (X, Y, Z)", beaconPos, 0.1f);
        if (ImGui::Button("Ping 3D Beacon at Position")) {
            PlayProcedural3DBeacon(beaconPos[0], beaconPos[1], beaconPos[2]);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(Move camera around to hear 3D panning)");

        ImGui::Spacing();
        ImGui::SeparatorText("Playback Controls");
        if (ImGui::Button("Stop All Sounds")) {
            StopAll();
        }
    }
    ImGui::End();
#endif
}

} // namespace EngineAudio
