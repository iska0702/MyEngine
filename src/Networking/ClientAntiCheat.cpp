#include "ClientAntiCheat.h"
#include "NetworkClient.h"
#include <iostream>

#ifndef ENGINE_SERVER
#include "../Security/SecuritySystem.h"
#endif

namespace EngineNetwork {

ClientAntiCheat::ClientAntiCheat() {}

ClientAntiCheat::~ClientAntiCheat() {
    Shutdown();
}

void ClientAntiCheat::Initialize(NetworkClient* client) {
    m_client = client;
    m_obfuscatedHealth = 100.0f;
    m_obfuscatedStamina = 100.0f;
}

void ClientAntiCheat::Shutdown() {
    m_client = nullptr;
}

bool ClientAntiCheat::PerformLocalIntegrityCheck(uint32_t& outSecurityFlags, uint64_t& outCodeHash) {
#ifndef ENGINE_SERVER
    return EngineSecurity::SecuritySystem::Get().PerformNetworkIntegrityCheck(outSecurityFlags, outCodeHash);
#else
    outSecurityFlags = IntegrityUnavailable;
    outCodeHash = 0;
    return false;
#endif
}

Packet ClientAntiCheat::HandleServerChallenge(uint32_t nonce) {
    uint32_t securityFlags = 0;
    uint64_t codeHash = 0;

    PerformLocalIntegrityCheck(securityFlags, codeHash);

    // Bind continuity telemetry to this challenge. This is not cryptographic attestation.
    uint64_t responseHash = codeHash ^ (static_cast<uint64_t>(nonce) * 1099511628211ULL);

    return Packet::CreateAntiCheatResponse(nonce, responseHash, securityFlags);
}

} // namespace EngineNetwork
