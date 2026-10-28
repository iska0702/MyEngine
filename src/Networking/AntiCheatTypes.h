#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <type_traits>
#ifdef _WIN32
#include <DirectXMath.h>
#else
#include "NetworkTypes.h"
#endif

namespace EngineNetwork {

// The current platform cannot produce a local code-integrity measurement.
inline constexpr uint32_t IntegrityUnavailable = 0x80000000u;

enum class CheatDetectionType : uint32_t {
    None = 0,
    Speedhack = 1,
    Teleportation = 2,
    Flyhack = 3,
    AirStuck = 4,
    InfiniteJump = 5,
    NoclipWallhack = 6,
    AimbotSnap = 7,
    RapidFire = 8,
    PacketSpam = 9,
    DeltaTimeManipulation = 10,
    ClientIntegrityTampered = 11,
    HeartbeatChallengeTimeout = 12
};

enum class ViolationSeverity : uint8_t {
    Info = 0,
    Warning = 1,
    Rubberband = 2,
    Kick = 3,
    Ban = 4
};

struct AntiCheatViolationEvent {
    uint32_t clientId = 0;
    std::string playerName = "Unknown";
    std::string clientIp = "127.0.0.1";
    CheatDetectionType type = CheatDetectionType::None;
    ViolationSeverity severity = ViolationSeverity::Info;
    std::string description;
    double timestamp = 0.0;
    DirectX::XMFLOAT3 attemptedPosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 correctedPosition = { 0.0f, 0.0f, 0.0f };
};

// Obfuscated value wrapper to protect client-side memory from Cheat Engine value scans
template<typename T>
class ObfuscatedValue {
    static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= sizeof(uint64_t),
                  "ObfuscatedValue requires a trivially copyable value of at most 8 bytes");
public:
    ObfuscatedValue() : ObfuscatedValue(T()) {}
    ObfuscatedValue(const T& val) {
        GenerateKey();
        Set(val);
    }

    void Set(const T& val) {
        uint64_t raw = 0;
        memcpy(&raw, &val, (sizeof(T) < sizeof(uint64_t) ? sizeof(T) : sizeof(uint64_t)));
        m_masked = raw ^ m_maskKey;
    }

    T Get() const {
        uint64_t raw = m_masked ^ m_maskKey;
        T val{};
        memcpy(&val, &raw, (sizeof(T) < sizeof(uint64_t) ? sizeof(T) : sizeof(uint64_t)));
        return val;
    }

    ObfuscatedValue& operator=(const T& val) {
        Set(val);
        return *this;
    }

    operator T() const {
        return Get();
    }

private:
    void GenerateKey() {
        m_maskKey = (static_cast<uint64_t>(rand()) << 32) ^ static_cast<uint64_t>(rand()) ^ 0xA5A5F0F05A5A0F0FULL;
        if (m_maskKey == 0) m_maskKey = 0x55AA55AA55AA55AAULL;
    }

    uint64_t m_masked = 0;
    uint64_t m_maskKey = 0;
};

} // namespace EngineNetwork
