#pragma once

#include "NetworkTypes.h"
#include "BitWriter.h"
#include "BitReader.h"
#include <deque>
#ifdef _WIN32
#include <DirectXMath.h>
#else
#include "NetworkTypes.h"
#endif

namespace EngineNetwork {

struct TransformSnapshot {
    double timestamp = 0.0;
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 rotation = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 velocity = { 0.0f, 0.0f, 0.0f };
    float yaw = 0.0f;
    float pitch = 0.0f;
    uint8_t flags = 0;
};

class NetworkObject {
public:
    NetworkObject(uint32_t netId = 0, uint32_t ownerId = 0);
    virtual ~NetworkObject() = default;

    uint32_t GetNetworkId() const { return m_networkId; }
    void SetNetworkId(uint32_t id) { m_networkId = id; }

    uint32_t GetOwnerClientId() const { return m_ownerClientId; }
    void SetOwnerClientId(uint32_t id) { m_ownerClientId = id; }

    const DirectX::XMFLOAT3& GetPosition() const { return m_position; }
    void SetPosition(const DirectX::XMFLOAT3& pos) { m_position = pos; m_isDirty = true; }

    const DirectX::XMFLOAT3& GetRotation() const { return m_rotation; }
    void SetRotation(const DirectX::XMFLOAT3& rot) { m_rotation = rot; m_isDirty = true; }

    const DirectX::XMFLOAT3& GetVelocity() const { return m_velocity; }
    void SetVelocity(const DirectX::XMFLOAT3& vel) { m_velocity = vel; }

    float GetYaw() const { return m_yaw; }
    void SetYaw(float yaw) { m_yaw = yaw; }

    float GetPitch() const { return m_pitch; }
    void SetPitch(float pitch) { m_pitch = pitch; }

    bool IsDirty() const { return m_isDirty; }
    void ClearDirty() { m_isDirty = false; }

    // Client-side snapshot interpolation
    void PushSnapshot(const TransformSnapshot& snap);
    void Interpolate(double renderTime, float interpolationDelaySeconds = 0.08f);

    virtual void Serialize(BitWriter& writer) const;
    virtual void Deserialize(BitReader& reader);

protected:
    uint32_t m_networkId = 0;
    uint32_t m_ownerClientId = 0;
    DirectX::XMFLOAT3 m_position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_rotation = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_velocity = { 0.0f, 0.0f, 0.0f };
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    bool m_isDirty = true;

    // Snapshot buffer for remote entity smoothing
    std::deque<TransformSnapshot> m_snapshotBuffer;
    static constexpr size_t MAX_SNAPSHOT_BUFFER_SIZE = 32;
};

} // namespace EngineNetwork
