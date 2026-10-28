#pragma once

#include "AntiCheatTypes.h"
#include "NetworkTypes.h"
#include "Packet.h"
#include <string>
#include <vector>
#include <cstdint>

namespace EngineNetwork {

class NetworkClient;

class ClientAntiCheat {
public:
    ClientAntiCheat();
    ~ClientAntiCheat();

    void Initialize(NetworkClient* client);
    void Shutdown();

    // Challenge-Response Attestation
    Packet HandleServerChallenge(uint32_t nonce);

    // Protected Game Variables
    void SetHealth(float health) { m_obfuscatedHealth = health; }
    float GetHealth() const { return m_obfuscatedHealth.Get(); }

    void SetStamina(float stamina) { m_obfuscatedStamina = stamina; }
    float GetStamina() const { return m_obfuscatedStamina.Get(); }

    // Client heartbeat integrity check
    bool PerformLocalIntegrityCheck(uint32_t& outSecurityFlags, uint64_t& outCodeHash);

private:
    NetworkClient* m_client = nullptr;
    ObfuscatedValue<float> m_obfuscatedHealth = 100.0f;
    ObfuscatedValue<float> m_obfuscatedStamina = 100.0f;
};

} // namespace EngineNetwork
