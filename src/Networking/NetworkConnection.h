#pragma once

#include "NetworkTypes.h"
#include "Packet.h"
#include "NetworkSocket.h"
#include <deque>
#include <unordered_map>
#include <chrono>

namespace EngineNetwork {

struct ReliablePacketEntry {
    uint16_t sequence = 0;
    Packet packet;
    double lastSentTime = 0.0;
    int retryCount = 0;
};

class NetworkConnection {
public:
    NetworkConnection();
    NetworkConnection(const NetworkAddress& address, uint32_t clientId = 0);

    const NetworkAddress& GetAddress() const { return m_address; }
    void SetAddress(const NetworkAddress& addr) { m_address = addr; }

    uint32_t GetClientId() const { return m_clientId; }
    void SetClientId(uint32_t id) { m_clientId = id; }

    ConnectionState GetState() const { return m_state; }
    void SetState(ConnectionState state) { m_state = state; }

    const std::string& GetPlayerName() const { return m_playerName; }
    void SetPlayerName(const std::string& name) { m_playerName = name; }

    float GetPingMs() const { return m_smoothedPingMs; }
    double GetLastReceivedTime() const { return m_lastReceivedTime; }

    // Packet sending through connection
    bool SendPacket(NetworkSocket& socket, Packet& packet, double currentTime);
    bool SendReliable(NetworkSocket& socket, Packet& packet, double currentTime);
    bool SendUnreliable(NetworkSocket& socket, Packet& packet, double currentTime);

    // Processing incoming raw packet
    bool ProcessIncomingHeader(const PacketHeader& header, double currentTime);

    // Updates reliable retransmissions, heartbeats, and timeout checks
    void Update(NetworkSocket& socket, double currentTime, double deltaTime);

    bool IsTimedOut(double currentTime, float timeoutSeconds = CONNECTION_TIMEOUT_SECONDS) const;
    void Reset();

private:
    void AcknowledgeSequence(uint16_t ack, uint32_t ackBitfield);
    uint32_t GenerateAckBitfield(uint16_t ack);

    NetworkAddress m_address;
    uint32_t m_clientId = 0;
    std::string m_playerName = "Player";
    ConnectionState m_state = ConnectionState::Disconnected;

    uint16_t m_localSequence = 0;
    uint16_t m_remoteSequence = 0;
    bool m_hasReceivedFirstPacket = false;

    // Received sequence history for ACK bitfield (store last 33 received sequence numbers)
    uint32_t m_receivedBitfield = 0;

    // Reliable packet retransmission queue
    std::unordered_map<uint16_t, ReliablePacketEntry> m_unackedReliablePackets;

    // Ping / RTT tracking
    float m_smoothedPingMs = 0.0f;
    double m_lastReceivedTime = 0.0;
    double m_lastSentTime = 0.0;
    double m_lastHeartbeatTime = 0.0;
    double m_rtt = 0.05; // Default 50ms estimate
};

} // namespace EngineNetwork
