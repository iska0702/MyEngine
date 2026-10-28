#include "ServerAntiCheat.h"
#include "NetworkServer.h"
#include "Packet.h"
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <fstream>
#ifdef _WIN32
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
#endif

namespace EngineNetwork {

ServerAntiCheat::ServerAntiCheat() {}

ServerAntiCheat::~ServerAntiCheat() {}

void ServerAntiCheat::Initialize(NetworkServer* server) {
    m_server = server;
    m_trackers.clear();
    m_recentViolations.clear();
    m_lastChallengeBroadcastTime = 0.0;
}

void ServerAntiCheat::RegisterClient(uint32_t clientId, const std::string& name, const std::string& ip, const DirectX::XMFLOAT3& initialPos) {
    ClientTracker tracker;
    tracker.clientId = clientId;
    tracker.playerName = name;
    tracker.ip = ip;
    tracker.lastValidPosition = initialPos;
    tracker.lastVelocity = { 0.0f, 0.0f, 0.0f };
    tracker.lastRateResetTime = -1.0;
    tracker.packetsThisSecond = 0;
    tracker.strikePoints = 0;
    tracker.airTimeSeconds = 0.0f;
    tracker.lastFireTime = 0.0;
    tracker.challengePending = false;

    m_trackers[clientId] = tracker;
}

void ServerAntiCheat::UnregisterClient(uint32_t clientId) {
    m_trackers.erase(clientId);
}

bool ServerAntiCheat::ValidateMovement(uint32_t clientId, const PlayerInputData& input, float dt,
                                      DirectX::XMFLOAT3& inOutPos, DirectX::XMFLOAT3& inOutVel, uint8_t& inOutFlags) {
    auto it = m_trackers.find(clientId);
    if (it == m_trackers.end()) return false;

    ClientTracker& tracker = it->second;

    const auto finite3 = [](const DirectX::XMFLOAT3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    if (!std::isfinite(dt) || dt <= 0.0f || dt > 0.1f || !finite3(inOutPos) || !finite3(inOutVel) ||
        !std::isfinite(input.lookYaw) || !std::isfinite(input.lookPitch)) {
        inOutPos = tracker.lastValidPosition;
        inOutVel = {0, 0, 0};
        inOutFlags &= ~PlayerFlags::IsFiring;
        ReportViolation(clientId, CheatDetectionType::DeltaTimeManipulation, ViolationSeverity::Kick,
                        "Invalid non-finite movement state or simulation interval");
        return false;
    }
    // This is server simulation time, including subdivided input batches.
    const float safeDt = dt;

    // 2. Speedhack & Teleportation Detection (Horizontal Displacement Limit)
    float dx = inOutPos.x - tracker.lastValidPosition.x;
    float dz = inOutPos.z - tracker.lastValidPosition.z;
    float distHorizontal = std::sqrt(dx * dx + dz * dz);

    // Max sprint speed in engine is 5.5 m/s. Allowing for air momentum & latency jitter: 8.5 m/s
    float maxAllowedSpeed = 8.5f;
    float maxAllowedDist = maxAllowedSpeed * safeDt + 0.40f;

    if (distHorizontal > maxAllowedDist) {
        DirectX::XMFLOAT3 attemptedPos = inOutPos;
        inOutPos = tracker.lastValidPosition; // Rubberband correction
        inOutVel.x = 0.0f;
        inOutVel.z = 0.0f;

        tracker.speedViolations++;
        tracker.strikePoints += 2;

        std::ostringstream ss;
        ss << "Speedhack / Teleportation Detected: moved " << distHorizontal << "m in " << safeDt << "s (max " << maxAllowedDist << "m)";
        ReportViolation(clientId, CheatDetectionType::Speedhack, ViolationSeverity::Rubberband, ss.str(), attemptedPos, inOutPos);
        return false;
    }

    // 3. Flyhack / Air-Stuck / Infinite Jump Detection
    if (!(inOutFlags & PlayerFlags::IsGrounded) && inOutPos.y > 1.05f) {
        tracker.airTimeSeconds += safeDt;
        // Natural jump apex is ~0.6s, total air arc < 1.8s
        if (tracker.airTimeSeconds > 2.2f && inOutVel.y >= -0.5f) {
            DirectX::XMFLOAT3 attemptedPos = inOutPos;
            inOutVel.y = -20.0f * safeDt;
            inOutPos.y = (std::max)(1.0f, inOutPos.y - 5.0f * safeDt);

            tracker.flyViolations++;
            tracker.strikePoints += 2;

            std::ostringstream ss;
            ss << "Flyhack / Gravity Defiance Detected: in air for " << tracker.airTimeSeconds << "s without falling";
            ReportViolation(clientId, CheatDetectionType::Flyhack, ViolationSeverity::Rubberband, ss.str(), attemptedPos, inOutPos);
            return false;
        }
    } else {
        tracker.airTimeSeconds = 0.0f;
    }

    // Fast mouse turns are valid input; angle deltas alone cannot prove an aimbot.
    // Update tracker state
    tracker.lastValidPosition = inOutPos;
    tracker.lastVelocity = inOutVel;
    tracker.lastYaw = input.lookYaw;
    tracker.lastPitch = input.lookPitch;
    tracker.lastProcessedTick = input.inputTick;

    return true;
}

bool ServerAntiCheat::ValidateCombatAction(uint32_t clientId, const PlayerInputData& input, double currentTime) {
    auto it = m_trackers.find(clientId);
    if (it == m_trackers.end() || !std::isfinite(currentTime)) return false;
    if (!input.fire) return true;
    ClientTracker& tracker = it->second;
    // fire is a held button, not a client claim that a shot already happened.
    // Enforce the weapon cooldown without issuing strikes for normal held input.
    if (tracker.hasFired && currentTime - tracker.lastFireTime < 0.10 - 1e-9) return false;
    tracker.hasFired = true;
    tracker.lastFireTime = currentTime;
    return true;
}

bool ServerAntiCheat::ValidatePacketRate(uint32_t clientId, double currentTime) {
    auto it = m_trackers.find(clientId);
    if (it == m_trackers.end()) return false;

    ClientTracker& tracker = it->second;
    if (!std::isfinite(currentTime)) return false;
    if (tracker.lastRateResetTime < 0.0 || currentTime - tracker.lastRateResetTime >= 1.0) {
        tracker.lastRateResetTime = currentTime;
        tracker.packetsThisSecond = 0;
    }

    if (tracker.packetsThisSecond >= 121) return false;
    tracker.packetsThisSecond++;

    // Max allowable client packet rate: 120 packets / sec
    if (tracker.packetsThisSecond > 120) {
        ReportViolation(clientId, CheatDetectionType::PacketSpam, ViolationSeverity::Kick,
                        "Packet Flood / DoS Attempt: " + std::to_string(tracker.packetsThisSecond) + " packets/sec");
        return false;
    }

    return true;
}

void ServerAntiCheat::SendIntegrityChallenges(double currentTime) {
    if (!std::isfinite(currentTime) || currentTime - m_lastChallengeBroadcastTime < 10.0) return;
    m_lastChallengeBroadcastTime = currentTime;

    for (auto& [id, tracker] : m_trackers) {
        if (tracker.challengePending) continue;
        uint32_t nonce=0;bool generated=false;
#ifdef _WIN32
        generated=BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&nonce),sizeof(nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0;
#else
        std::ifstream random("/dev/urandom",std::ios::binary);generated=bool(random.read(reinterpret_cast<char*>(&nonce),sizeof(nonce)));
#endif
        if(!generated){std::cerr<<"AntiCheat: secure nonce generation failed\n";continue;}
        tracker.pendingNonce = nonce;
        tracker.challengePending = true;
        tracker.challengeSentTime = currentTime;

        Packet challenge = Packet::CreateAntiCheatChallenge(nonce);
        if (m_server) {
            m_server->SendToClient(id, challenge, true);
        }
    }
}

void ServerAntiCheat::HandleIntegrityResponse(uint32_t clientId, uint32_t nonce, uint64_t codeHash, uint32_t securityFlags, double currentTime) {
    auto it = m_trackers.find(clientId);
    if (it == m_trackers.end()) return;

    ClientTracker& tracker = it->second;
    // UDP may deliver an old/repeated reply after the request was completed.
    if (!tracker.challengePending || tracker.pendingNonce != nonce || !std::isfinite(currentTime)) return;
    tracker.challengePending = false;
    if (currentTime < tracker.challengeSentTime || currentTime - tracker.challengeSentTime > 8.0) {
        ReportViolation(clientId, CheatDetectionType::HeartbeatChallengeTimeout, ViolationSeverity::Kick,
                        "Integrity challenge response arrived outside its deadline");
        return;
    }
    const uint64_t reportedHash = codeHash ^ (static_cast<uint64_t>(nonce) * 1099511628211ULL);
    if (securityFlags == IntegrityUnavailable) return; // Liveness only; never call this verified.
    if (securityFlags != 0 || reportedHash == 0 ||
        (tracker.sessionCodeHash != 0 && tracker.sessionCodeHash != reportedHash)) {
        // Client reports are untrusted telemetry. Never permanently ban an IP from them.
        ReportViolation(clientId, CheatDetectionType::ClientIntegrityTampered, ViolationSeverity::Kick,
                        "Client reported an integrity failure or changed session code hash");
        return;
    }
    tracker.sessionCodeHash = reportedHash;

}

void ServerAntiCheat::Update(double currentTime) {
    if (!std::isfinite(currentTime)) return;
    // Expire before issuing another challenge: a long frame must not reset the deadline.
    std::vector<uint32_t> expired;
    for (auto& [id, tracker] : m_trackers) {
        if (tracker.challengePending && currentTime - tracker.challengeSentTime > 8.0) {
            tracker.challengePending = false;
            expired.push_back(id);
        }
    }
    // Reporting may remove clients when called outside NetworkServer::Update.
    for (uint32_t id : expired)
        ReportViolation(id, CheatDetectionType::HeartbeatChallengeTimeout, ViolationSeverity::Kick,
                        "Client failed to respond to integrity challenge before deadline");
    SendIntegrityChallenges(currentTime);

}

void ServerAntiCheat::ReportViolation(uint32_t clientId, CheatDetectionType type, ViolationSeverity severity,
                                      const std::string& reason, const DirectX::XMFLOAT3& attemptedPos,
                                      const DirectX::XMFLOAT3& correctedPos) {
    auto it = m_trackers.find(clientId);
    std::string pName = (it != m_trackers.end()) ? it->second.playerName : "Unknown";
    std::string ip = (it != m_trackers.end()) ? it->second.ip : "127.0.0.1";

    AntiCheatViolationEvent ev;
    ev.clientId = clientId;
    ev.playerName = pName;
    ev.clientIp = ip;
    ev.type = type;
    ev.severity = severity;
    ev.description = reason;
    ev.timestamp = m_server ? m_server->GetUptimeSeconds() : 0.0;
    ev.attemptedPosition = attemptedPos;
    ev.correctedPosition = correctedPos;

    m_recentViolations.push_back(ev);
    if (m_recentViolations.size() > 100) {
        m_recentViolations.erase(m_recentViolations.begin());
    }

    std::string logMsg = "[ANTI-CHEAT] Client #" + std::to_string(clientId) + " (" + pName + " @ " + ip + ") VIOLATION: " + reason;
    std::cout << logMsg << std::endl;

    // Strike Threshold Enforcement
    uint32_t currentStrikes = (it != m_trackers.end()) ? it->second.strikePoints : 0;
    if (severity == ViolationSeverity::Ban) {
        if (m_server) {
            m_server->BanPlayer(clientId, "Anti-Cheat Enforcement: " + reason);
            m_server->BanIP(ip);
        }
    } else if (severity == ViolationSeverity::Kick || currentStrikes >= 6) {
        if (m_server) {
            m_server->KickPlayer(clientId, "Anti-Cheat Enforcement: " + reason);
        }
    }
}

} // namespace EngineNetwork
