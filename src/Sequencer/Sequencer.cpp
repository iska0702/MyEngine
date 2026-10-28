#include "Sequencer.h"
#include "Renderer.h"
#include <fstream>
#include <sstream>
#include <algorithm>

// ---- Track ----

void Track::SortKeyframes() {
    std::sort(keyframes.begin(), keyframes.end(),
              [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
}

void Track::AddKeyframe(const Keyframe& kf) {
    // Replace if same time exists
    for (auto& existing : keyframes) {
        if (fabsf(existing.time - kf.time) < 0.001f) {
            existing = kf;
            return;
        }
    }
    keyframes.push_back(kf);
    SortKeyframes();
}

void Track::RemoveKeyframe(int index) {
    if (index >= 0 && index < (int)keyframes.size()) {
        keyframes.erase(keyframes.begin() + index);
    }
}

static XMFLOAT3 LerpFloat3(const XMFLOAT3& a, const XMFLOAT3& b, float t) {
    return XMFLOAT3(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    );
}

bool Track::Evaluate(float time, XMFLOAT3& outPos, XMFLOAT3& outRot, XMFLOAT3& outScale) const {
    if (keyframes.empty()) return false;

    if (keyframes.size() == 1 || time <= keyframes.front().time) {
        outPos = keyframes.front().position;
        outRot = keyframes.front().rotation;
        outScale = keyframes.front().scale;
        return true;
    }

    if (time >= keyframes.back().time) {
        outPos = keyframes.back().position;
        outRot = keyframes.back().rotation;
        outScale = keyframes.back().scale;
        return true;
    }

    // Find two keyframes to interpolate between
    for (size_t i = 0; i < keyframes.size() - 1; ++i) {
        if (time >= keyframes[i].time && time <= keyframes[i + 1].time) {
            float duration = keyframes[i + 1].time - keyframes[i].time;
            float t = (duration > 0.0001f) ? (time - keyframes[i].time) / duration : 0.0f;
            outPos = LerpFloat3(keyframes[i].position, keyframes[i + 1].position, t);
            outRot = LerpFloat3(keyframes[i].rotation, keyframes[i + 1].rotation, t);
            outScale = LerpFloat3(keyframes[i].scale, keyframes[i + 1].scale, t);
            return true;
        }
    }
    return false;
}

// ---- Sequencer ----

Sequencer::Sequencer() {}

void Sequencer::Play() {
    m_playing = true;
    m_paused = false;
}

void Sequencer::Pause() {
    if (m_playing) {
        m_paused = true;
        m_playing = false;
    }
}

void Sequencer::Stop() {
    m_playing = false;
    m_paused = false;
    m_currentTime = 0.0f;
}

void Sequencer::SetTime(float t) {
    m_currentTime = t;
    if (m_currentTime < 0) m_currentTime = 0;
    if (m_currentTime > m_duration) m_currentTime = m_duration;
}

void Sequencer::RemoveTrack(int index) {
    if (index >= 0 && index < (int)m_tracks.size()) {
        m_tracks.erase(m_tracks.begin() + index);
    }
}

Track* Sequencer::GetOrCreateTrack(const std::string& modelName) {
    for (auto& track : m_tracks) {
        if (track.modelName == modelName) return &track;
    }
    m_tracks.push_back({});
    m_tracks.back().modelName = modelName;
    return &m_tracks.back();
}

void Sequencer::CaptureKeyframe(const std::string& modelName, float time,
                                 XMFLOAT3 pos, XMFLOAT3 rot, XMFLOAT3 scale) {
    Track* track = GetOrCreateTrack(modelName);
    track->AddKeyframe(Keyframe(time, pos, rot, scale));
}

void Sequencer::Update(float dt, Renderer* renderer) {
    if (m_playing && !m_paused) {
        m_currentTime += dt;
        if (m_currentTime > m_duration) {
            m_currentTime = 0.0f; // Loop
        }
    }

    // Apply tracks to models
    auto& models = renderer->GetModels();
    for (auto& track : m_tracks) {
        XMFLOAT3 pos, rot, scale;
        if (track.Evaluate(m_currentTime, pos, rot, scale)) {
            for (auto& model : models) {
                if (model.name == track.modelName) {
                    model.position = pos;
                    model.rotation = rot;
                    model.scale = scale;
                    break;
                }
            }
            // Also check dynamic models
            auto& dynModels = renderer->GetDynamicModels();
            for (auto& model : dynModels) {
                if (model.name == track.modelName) {
                    model.position = pos;
                    model.rotation = rot;
                    model.scale = scale;
                    break;
                }
            }
        }
    }
}

void Sequencer::Save(const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) return;

    file << "DURATION " << m_duration << "\n";
    for (const auto& track : m_tracks) {
        file << "TRACK " << track.modelName << "\n";
        for (const auto& kf : track.keyframes) {
            file << "KF " << kf.time
                 << " " << kf.position.x << " " << kf.position.y << " " << kf.position.z
                 << " " << kf.rotation.x << " " << kf.rotation.y << " " << kf.rotation.z
                 << " " << kf.scale.x << " " << kf.scale.y << " " << kf.scale.z
                 << "\n";
        }
    }
}

void Sequencer::Load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return;

    m_tracks.clear();
    Track* currentTrack = nullptr;
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string tag;
        iss >> tag;

        if (tag == "DURATION") {
            iss >> m_duration;
        } else if (tag == "TRACK") {
            std::string name;
            std::getline(iss >> std::ws, name);
            m_tracks.push_back({});
            m_tracks.back().modelName = name;
            currentTrack = &m_tracks.back();
        } else if (tag == "KF" && currentTrack) {
            Keyframe kf;
            iss >> kf.time
                >> kf.position.x >> kf.position.y >> kf.position.z
                >> kf.rotation.x >> kf.rotation.y >> kf.rotation.z
                >> kf.scale.x >> kf.scale.y >> kf.scale.z;
            currentTrack->keyframes.push_back(kf);
        }
    }
}
