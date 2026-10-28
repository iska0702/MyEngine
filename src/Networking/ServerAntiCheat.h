#pragma once

#include "AntiCheatTypes.h"
#include "NetworkTypes.h"
#include <unordered_map>
#include <vector>
#include <string>
#ifdef _WIN32
#include <DirectXMath.h>
#else
#include "NetworkTypes.h"
#endif

namespace EngineNetwork {

class NetworkServer;

struct ClientTracker {
    uint32_t clientId = 0;
    std::string playerName = "Unknown";
    std::string ip = "127.0.0.1";

    DirectX::XMFLOAT3 lastValidPosition = { 0.0f, 1.0f, 0.0f };
    DirectX::XMFLOAT3 lastVelocity = { 0.0f, 0.0f, 0.0f };
    uint32_t lastProcessedTick = 0;
    float airTimeSeconds = 0.0f;
    float lastYaw = 0.0f;
    float lastPitch = 0.0f;
    double lastFireTime = 0.0;
    bool hasFired = false;

    // Packet Rate Limiting
    double lastRateResetTime = -1.0;
    uint32_t packetsThisSecond = 0;

    // Challenge-Response Attestation
    uint64_t sessionCodeHash = 0; // Continuity telemetry, not remote attestation.
    uint32_t pendingNonce = 0;
    bool challengePending = false;
    double challengeSentTime = 0.0;
    double lastChallengeTime = 0.0;

    // Violation Strike Tracking
    uint32_t strikePoints = 0;
    uint32_t speedViolations = 0;
    uint32_t flyViolations = 0;
    uint32_t noclipViolations = 0;
    uint32_t aimViolations = 0;
    uint32_t rapidFireViolations = 0;
};

class ServerAntiCheat {
public:
    ServerAntiCheat();
    ~ServerAntiCheat();

    void Initialize(NetworkServer* server);
    void RegisterClient(uint32_t clientId, const std::string& name, const std::string& ip, const DirectX::XMFLOAT3& initialPos);
    void UnregisterClient(uint32_t clientId);

    // 1. Authoritative Movement & Physics Validation
    bool ValidateMovement(uint32_t clientId, const PlayerInputData& input, float dt,
                          DirectX::XMFLOAT3& inOutPos, DirectX::XMFLOAT3& inOutVel, uint8_t& inOutFlags);

    // 2. Authoritative Combat & Weapon Cooldown Validation
    bool ValidateCombatAction(uint32_t clientId, const PlayerInputData& input, double currentTime);

    // 3. DoS & Packet Spam Rate Limiting
    bool ValidatePacketRate(uint32_t clientId, double currentTime);

    // 4. Client Binary & Code Integrity Challenge-Response
    void SendIntegrityChallenges(double currentTime);
    void HandleIntegrityResponse(uint32_t clientId, uint32_t nonce, uint64_t codeHash, uint32_t securityFlags, double currentTime);

    // 5. Periodic Heartbeat / Cleanup
    void Update(double currentTime);

    void ReportViolation(uint32_t clientId, CheatDetectionType type, ViolationSeverity severity,
                         const std::string& reason, const DirectX::XMFLOAT3& attemptedPos = { 0, 0, 0 },
                         const DirectX::XMFLOAT3& correctedPos = { 0, 0, 0 });

    const std::vector<AntiCheatViolationEvent>& GetRecentViolations() const { return m_recentViolations; }

private:
    NetworkServer* m_server = nullptr;
    std::unordered_map<uint32_t, ClientTracker> m_trackers;
    std::vector<AntiCheatViolationEvent> m_recentViolations;
    double m_lastChallengeBroadcastTime = 0.0;
};

} // namespace EngineNetwork
