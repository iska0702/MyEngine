#pragma once

#include "NetworkTypes.h"
#include "BitWriter.h"
#include "BitReader.h"
#include <vector>
#include <memory>

namespace EngineNetwork {

class Packet {
public:
    Packet();
    Packet(PacketType type, PacketChannel channel = PacketChannel::Unreliable);

    PacketHeader header;
    BitWriter payload;

    // Serialize header + payload into raw byte buffer
    std::vector<uint8_t> Serialize() const;

    // Deserialize raw byte buffer into header and payload reader
    static bool Deserialize(const void* data, size_t size, PacketHeader& outHeader, BitReader& outReader);

    // High-level packet factories
    static Packet CreateConnectionRequest(const std::string& playerName, uint32_t clientVersion = 2);
    static Packet CreateConnectionAccepted(uint32_t assignedClientId, uint32_t serverTick, float tickRate);
    static Packet CreateConnectionRejected(RejectReason reason, const std::string& message = "");
    static Packet CreateDisconnect(uint32_t clientId, const std::string& reason = "Client Disconnect");
    static Packet CreatePing(uint32_t pingId, double clientTimestamp);
    static Packet CreatePong(uint32_t pingId, double clientTimestamp, double serverTimestamp);
    static Packet CreatePlayerInput(const PlayerInputData& input);
    static Packet CreateWorldSnapshot(const WorldSnapshotData& snapshot);
    static std::vector<Packet> CreateWorldSnapshotChunks(const WorldSnapshotData& snapshot);
    static Packet CreateSpawnObject(const DynamicObjectStateData& obj);
    static Packet CreateDestroyObject(uint32_t networkId);
    static Packet CreateChatMessage(uint32_t senderId, const std::string& senderName, const std::string& message);
    static Packet CreateServerCommand(const std::string& command);
    static Packet CreateHeartbeat();
    static Packet CreateAntiCheatChallenge(uint32_t nonce);
    static Packet CreateAntiCheatResponse(uint32_t nonce, uint64_t codeHash, uint32_t securityFlags);

    // Deserialization helpers
    static bool ReadConnectionRequest(BitReader& reader, std::string& outPlayerName, uint32_t& outVersion);
    static bool ReadConnectionAccepted(BitReader& reader, uint32_t& outClientId, uint32_t& outServerTick, float& outTickRate);
    static bool ReadConnectionRejected(BitReader& reader, RejectReason& outReason, std::string& outMessage);
    static bool ReadDisconnect(BitReader& reader, uint32_t& outClientId, std::string& outReason);
    static bool ReadPing(BitReader& reader, uint32_t& outPingId, double& outTimestamp);
    static bool ReadPong(BitReader& reader, uint32_t& outPingId, double& outClientTimestamp, double& outServerTimestamp);
    static bool ReadPlayerInput(BitReader& reader, PlayerInputData& outInput);
    static bool ReadWorldSnapshot(BitReader& reader, WorldSnapshotData& outSnapshot);
    static bool ReadSpawnObject(BitReader& reader, DynamicObjectStateData& outObj);
    static bool ReadDestroyObject(BitReader& reader, uint32_t& outNetworkId);
    static bool ReadChatMessage(BitReader& reader, uint32_t& outSenderId, std::string& outSenderName, std::string& outMessage);
    static bool ReadServerCommand(BitReader& reader, std::string& outCommand);
    static bool ReadAntiCheatChallenge(BitReader& reader, uint32_t& outNonce);
    static bool ReadAntiCheatResponse(BitReader& reader, uint32_t& outNonce, uint64_t& outCodeHash, uint32_t& outSecurityFlags);
};

} // namespace EngineNetwork
