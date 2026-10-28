#include "NetworkObject.h"
#include <algorithm>
#include <cmath>

namespace EngineNetwork {

NetworkObject::NetworkObject(uint32_t netId, uint32_t ownerId)
    : m_networkId(netId), m_ownerClientId(ownerId) {}

void NetworkObject::PushSnapshot(const TransformSnapshot& snap) {
    m_snapshotBuffer.push_back(snap);
    while (m_snapshotBuffer.size() > MAX_SNAPSHOT_BUFFER_SIZE) {
        m_snapshotBuffer.pop_front();
    }
}

static inline float LerpFloat(float a, float b, float t) {
    return a + t * (b - a);
}

static inline DirectX::XMFLOAT3 LerpVector3(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float t) {
    return DirectX::XMFLOAT3(
        LerpFloat(a.x, b.x, t),
        LerpFloat(a.y, b.y, t),
        LerpFloat(a.z, b.z, t)
    );
}

// Shortest-path angle interpolation
static inline float LerpAngle(float from, float to, float t) {
    float diff = to - from;
    while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;
    while (diff > DirectX::XM_PI) diff -= DirectX::XM_2PI;
    return from + diff * t;
}

void NetworkObject::Interpolate(double renderTime, float interpolationDelaySeconds) {
    if (m_snapshotBuffer.empty()) return;

    double targetTime = renderTime - interpolationDelaySeconds;

    // Single snapshot: direct assignment
    if (m_snapshotBuffer.size() == 1) {
        m_position = m_snapshotBuffer.front().position;
        m_yaw = m_snapshotBuffer.front().yaw;
        m_pitch = m_snapshotBuffer.front().pitch;
        m_velocity = m_snapshotBuffer.front().velocity;
        return;
    }

    // Target time is older than oldest snapshot
    if (targetTime <= m_snapshotBuffer.front().timestamp) {
        m_position = m_snapshotBuffer.front().position;
        m_yaw = m_snapshotBuffer.front().yaw;
        m_pitch = m_snapshotBuffer.front().pitch;
        m_velocity = m_snapshotBuffer.front().velocity;
        return;
    }

    // Target time is newer than newest snapshot -> Extrapolate slightly based on velocity
    if (targetTime >= m_snapshotBuffer.back().timestamp) {
        const auto& newest = m_snapshotBuffer.back();
        float dt = static_cast<float>((std::min)(targetTime - newest.timestamp, 0.2));
        m_position.x = newest.position.x + newest.velocity.x * dt;
        m_position.y = newest.position.y + newest.velocity.y * dt;
        m_position.z = newest.position.z + newest.velocity.z * dt;
        m_yaw = newest.yaw;
        m_pitch = newest.pitch;
        m_velocity = newest.velocity;
        return;
    }

    // Find bounding snapshots [s0, s1] such that s0.timestamp <= targetTime <= s1.timestamp
    for (size_t i = 0; i < m_snapshotBuffer.size() - 1; ++i) {
        const auto& s0 = m_snapshotBuffer[i];
        const auto& s1 = m_snapshotBuffer[i + 1];

        if (targetTime >= s0.timestamp && targetTime <= s1.timestamp) {
            double totalSpan = s1.timestamp - s0.timestamp;
            float alpha = 0.0f;
            if (totalSpan > 0.0001) {
                alpha = static_cast<float>((targetTime - s0.timestamp) / totalSpan);
            }
            alpha = (std::clamp)(alpha, 0.0f, 1.0f);

            m_position = LerpVector3(s0.position, s1.position, alpha);
            m_yaw = LerpAngle(s0.yaw, s1.yaw, alpha);
            m_pitch = LerpAngle(s0.pitch, s1.pitch, alpha);
            m_velocity = LerpVector3(s0.velocity, s1.velocity, alpha);
            return;
        }
    }
}

void NetworkObject::Serialize(BitWriter& writer) const {
    writer.WriteUInt32(m_networkId);
    writer.WriteUInt32(m_ownerClientId);
    writer.WriteVector3(m_position);
    writer.WriteVector3(m_velocity);
    writer.WriteFloat(m_yaw);
    writer.WriteFloat(m_pitch);
}

void NetworkObject::Deserialize(BitReader& reader) {
    reader.ReadUInt32(m_networkId);
    reader.ReadUInt32(m_ownerClientId);
    reader.ReadVector3(m_position);
    reader.ReadVector3(m_velocity);
    reader.ReadFloat(m_yaw);
    reader.ReadFloat(m_pitch);
}

} // namespace EngineNetwork
