#pragma once
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <map>

using namespace DirectX;

struct Keyframe {
    float time;
    XMFLOAT3 position;
    XMFLOAT3 rotation;
    XMFLOAT3 scale;

    Keyframe() : time(0), position(0,0,0), rotation(0,0,0), scale(1,1,1) {}
    Keyframe(float t, XMFLOAT3 p, XMFLOAT3 r, XMFLOAT3 s)
        : time(t), position(p), rotation(r), scale(s) {}
};

struct Track {
    std::string modelName;
    std::vector<Keyframe> keyframes;

    void SortKeyframes();
    void AddKeyframe(const Keyframe& kf);
    void RemoveKeyframe(int index);
    bool Evaluate(float time, XMFLOAT3& outPos, XMFLOAT3& outRot, XMFLOAT3& outScale) const;
};

class Renderer;

class Sequencer {
public:
    Sequencer();

    void Play();
    void Pause();
    void Stop();
    bool IsPlaying() const { return m_playing; }
    bool IsPaused() const { return m_paused; }

    void SetTime(float t);
    float GetTime() const { return m_currentTime; }
    float GetDuration() const { return m_duration; }
    void SetDuration(float d) { m_duration = d; }

    void Update(float dt, Renderer* renderer);

    std::vector<Track>& GetTracks() { return m_tracks; }
    void RemoveTrack(int index);

    // Find or create track for a model
    Track* GetOrCreateTrack(const std::string& modelName);

    // Add keyframe from current model state
    void CaptureKeyframe(const std::string& modelName, float time,
                         XMFLOAT3 pos, XMFLOAT3 rot, XMFLOAT3 scale);

    void Save(const std::string& filepath);
    void Load(const std::string& filepath);

private:
    std::vector<Track> m_tracks;
    float m_currentTime = 0.0f;
    float m_duration = 10.0f;
    bool m_playing = false;
    bool m_paused = false;
};
