#include "NetworkConnection.h"
#include <algorithm>
#include <iostream>
#include <cmath>

namespace EngineNetwork {

// Helper sequence comparison taking 16-bit wrapping into account
static inline bool SequenceGreaterThan(uint16_t s1, uint16_t s2) {
    return ((s1 > s2) && (s1 - s2 <= 32768)) || ((s1 < s2) && (s2 - s1 > 32768));
}

static inline int SequenceDifference(uint16_t s1, uint16_t s2) {
    int diff = static_cast<int>(s1) - static_cast<int>(s2);
    if (diff > 32768) diff -= 65536;
    if (diff < -32768) diff += 65536;
    return diff;
}

NetworkConnection::NetworkConnection() {}

NetworkConnection::NetworkConnection(const NetworkAddress& address, uint32_t clientId)
    : m_address(address), m_clientId(clientId) {}

void NetworkConnection::Reset() {
    m_state = ConnectionState::Disconnected;
    m_localSequence = 0;
    m_remoteSequence = 0;
    m_hasReceivedFirstPacket = false;
    m_receivedBitfield = 0;
    m_unackedReliablePackets.clear();
    m_smoothedPingMs = 0.0f;
    m_lastReceivedTime = 0.0;
    m_lastSentTime = 0.0;
    m_lastHeartbeatTime = 0.0;
    m_rtt = 0.05;
}

bool NetworkConnection::SendPacket(NetworkSocket& socket, Packet& packet, double currentTime) {
    if(packet.header.channel==static_cast<uint8_t>(PacketChannel::Reliable)&&m_unackedReliablePackets.size()>=256)return false;
    packet.header.sequence = m_localSequence++;
    packet.header.ack = m_remoteSequence;
    packet.header.ackBitfield = m_receivedBitfield;

    std::vector<uint8_t> raw = packet.Serialize();
    bool sent = socket.Send(m_address, raw.data(), raw.size());
    if (sent) {
        m_lastSentTime = currentTime;

        if (packet.header.channel == static_cast<uint8_t>(PacketChannel::Reliable) && !socket.IsTls()) {
            ReliablePacketEntry entry;
            entry.sequence = packet.header.sequence;
            entry.packet = packet;
            entry.lastSentTime = currentTime;
            entry.retryCount = 0;
            m_unackedReliablePackets[entry.sequence] = entry;
        }
    }
    return sent;
}

bool NetworkConnection::SendReliable(NetworkSocket& socket, Packet& packet, double currentTime) {
    packet.header.channel = static_cast<uint8_t>(PacketChannel::Reliable);
    return SendPacket(socket, packet, currentTime);
}

bool NetworkConnection::SendUnreliable(NetworkSocket& socket, Packet& packet, double currentTime) {
    packet.header.channel = static_cast<uint8_t>(PacketChannel::Unreliable);
    return SendPacket(socket, packet, currentTime);
}

bool NetworkConnection::ProcessIncomingHeader(const PacketHeader& header, double currentTime) {
    if(!std::isfinite(currentTime))return false;

    // First packet received
    if (!m_hasReceivedFirstPacket) {
        m_remoteSequence = header.sequence;
        m_receivedBitfield = 0;
        m_hasReceivedFirstPacket = true;
    } else {
        if (SequenceGreaterThan(header.sequence, m_remoteSequence)) {
            int diff = SequenceDifference(header.sequence, m_remoteSequence);
            if (diff > 0 && diff <= 32) {
                m_receivedBitfield = (diff == 32 ? 0u : m_receivedBitfield << diff) | (1u << (diff - 1));
            } else if (diff > 32) {
                m_receivedBitfield = 0;
            }
            m_remoteSequence = header.sequence;
        } else {
            int diff = SequenceDifference(m_remoteSequence, header.sequence);
            if (diff == 0 || diff > 32) return false;
            if (diff > 0 && diff <= 32) {
                if (m_receivedBitfield & (1u << (diff - 1))) return false;
                m_receivedBitfield |= (1u << (diff - 1));
            }
        }
    }

    m_lastReceivedTime=currentTime;
    AcknowledgeSequence(header.ack,header.ackBitfield);
    return true;
}

void NetworkConnection::AcknowledgeSequence(uint16_t ack, uint32_t ackBitfield) {
    // Ack exact sequence
    m_unackedReliablePackets.erase(ack);

    // Ack previous 32 sequence entries via bitfield
    for (int i = 0; i < 32; ++i) {
        if ((ackBitfield & (1u << i)) != 0) {
            uint16_t prevSeq = ack - static_cast<uint16_t>(i + 1);
            m_unackedReliablePackets.erase(prevSeq);
        }
    }
}

void NetworkConnection::Update(NetworkSocket& socket, double currentTime, double deltaTime) {
    // Retransmit unacknowledged reliable packets
    double rto = (std::max)(m_rtt * 1.5 + 0.05, 0.10); // Retransmission timeout (minimum 100ms)

    for(auto it=m_unackedReliablePackets.begin();it!=m_unackedReliablePackets.end();){
        auto& seq=it->first;auto& entry=it->second;
        if(entry.retryCount>=10&&currentTime-entry.lastSentTime>=rto){it=m_unackedReliablePackets.erase(it);continue;}
        ++it;
        if (currentTime - entry.lastSentTime >= rto) {
            if (entry.retryCount < 10) {
                entry.lastSentTime = currentTime;
                entry.retryCount++;

                // Resend packet updating ack headers
                entry.packet.header.ack = m_remoteSequence;
                entry.packet.header.ackBitfield = m_receivedBitfield;
                std::vector<uint8_t> raw = entry.packet.Serialize();
                socket.Send(m_address, raw.data(), raw.size());
            }
        }
    }

    // Send periodic heartbeats if no traffic sent recently
    if (m_state == ConnectionState::Connected) {
        if (currentTime - m_lastSentTime >= HEARTBEAT_INTERVAL_SECONDS) {
            Packet hb = Packet::CreateHeartbeat();
            SendUnreliable(socket, hb, currentTime);
        }
    }
}

bool NetworkConnection::IsTimedOut(double currentTime, float timeoutSeconds) const {
    if (m_state == ConnectionState::Disconnected) return false;
    if (!m_hasReceivedFirstPacket) return false;
    return (currentTime - m_lastReceivedTime) > timeoutSeconds;
}

} // namespace EngineNetwork
