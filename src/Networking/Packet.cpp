#include "Packet.h"
#include <algorithm>
#include <limits>

namespace EngineNetwork {

Packet::Packet() {
    header.protocolId = PROTOCOL_ID;
    header.packetType = static_cast<uint8_t>(PacketType::None);
    header.channel = static_cast<uint8_t>(PacketChannel::Unreliable);
}

Packet::Packet(PacketType type, PacketChannel channel) {
    header.protocolId = PROTOCOL_ID;
    header.packetType = static_cast<uint8_t>(type);
    header.channel = static_cast<uint8_t>(channel);
}

std::vector<uint8_t> Packet::Serialize() const {
    const_cast<Packet*>(this)->payload.FlushBits();
    size_t payloadSize = payload.GetByteSize();
    if (payloadSize > (std::numeric_limits<uint16_t>::max)()) return {};
    
    PacketHeader hdr = header;
    hdr.payloadSize = static_cast<uint16_t>(payloadSize);

    std::vector<uint8_t> buffer(sizeof(PacketHeader) + payloadSize);
    memcpy(buffer.data(), &hdr, sizeof(PacketHeader));
    if (payloadSize > 0) {
        memcpy(buffer.data() + sizeof(PacketHeader), payload.GetData(), payloadSize);
    }
    return buffer;
}

bool Packet::Deserialize(const void* data, size_t size, PacketHeader& outHeader, BitReader& outReader) {
    if (!data || size < sizeof(PacketHeader)) return false;

    memcpy(&outHeader, data, sizeof(PacketHeader));
    if (outHeader.protocolId != PROTOCOL_ID) return false;

    if (sizeof(PacketHeader) + outHeader.payloadSize != size) return false;

    const uint8_t* payloadPtr = static_cast<const uint8_t*>(data) + sizeof(PacketHeader);
    outReader.SetBuffer(payloadPtr, outHeader.payloadSize);
    return true;
}

Packet Packet::CreateConnectionRequest(const std::string& playerName, uint32_t clientVersion) {
    Packet p(PacketType::ConnectionRequest, PacketChannel::Reliable);
    p.payload.WriteString(playerName);
    p.payload.WriteUInt32(clientVersion);
    return p;
}

Packet Packet::CreateConnectionAccepted(uint32_t assignedClientId, uint32_t serverTick, float tickRate) {
    Packet p(PacketType::ConnectionAccepted, PacketChannel::Reliable);
    p.payload.WriteUInt32(assignedClientId);
    p.payload.WriteUInt32(serverTick);
    p.payload.WriteFloat(tickRate);
    return p;
}

Packet Packet::CreateConnectionRejected(RejectReason reason, const std::string& message) {
    Packet p(PacketType::ConnectionRejected, PacketChannel::Reliable);
    p.payload.WriteUInt8(static_cast<uint8_t>(reason));
    p.payload.WriteString(message);
    return p;
}

Packet Packet::CreateDisconnect(uint32_t clientId, const std::string& reason) {
    Packet p(PacketType::Disconnect, PacketChannel::Reliable);
    p.payload.WriteUInt32(clientId);
    p.payload.WriteString(reason);
    return p;
}

Packet Packet::CreatePing(uint32_t pingId, double clientTimestamp) {
    Packet p(PacketType::Ping, PacketChannel::Unreliable);
    p.payload.WriteUInt32(pingId);
    p.payload.WriteDouble(clientTimestamp);
    return p;
}

Packet Packet::CreatePong(uint32_t pingId, double clientTimestamp, double serverTimestamp) {
    Packet p(PacketType::Pong, PacketChannel::Unreliable);
    p.payload.WriteUInt32(pingId);
    p.payload.WriteDouble(clientTimestamp);
    p.payload.WriteDouble(serverTimestamp);
    return p;
}

Packet Packet::CreatePlayerInput(const PlayerInputData& input) {
    Packet p(PacketType::PlayerInput, PacketChannel::Unreliable);
    p.payload.WriteUInt32(input.inputTick);
    p.payload.WriteFloat(input.moveX);
    p.payload.WriteFloat(input.moveY);
    p.payload.WriteFloat(input.lookYaw);
    p.payload.WriteFloat(input.lookPitch);
    p.payload.WriteBool(input.jump);
    p.payload.WriteBool(input.crouch);
    p.payload.WriteBool(input.sprint);
    p.payload.WriteBool(input.fire);
    p.payload.WriteFloat(input.deltaTime);
    return p;
}

Packet Packet::CreateWorldSnapshot(const WorldSnapshotData& snapshot) {
    Packet p(PacketType::WorldSnapshot, PacketChannel::Unreliable);
    p.payload.WriteUInt32(snapshot.serverTick);
    p.payload.WriteUInt32(snapshot.lastAckedInputTick);
    p.payload.WriteDouble(snapshot.timestamp);

    // Write players
    p.payload.WriteUInt16(static_cast<uint16_t>(snapshot.players.size()));
    for (const auto& player : snapshot.players) {
        p.payload.WriteUInt32(player.networkId);
        p.payload.WriteUInt32(player.ownerClientId);
        p.payload.WriteString(player.playerName);
        p.payload.WriteVector3(player.position);
        p.payload.WriteVector3(player.velocity);
        p.payload.WriteFloat(player.yaw);
        p.payload.WriteFloat(player.pitch);
        p.payload.WriteFloat(player.health);
        p.payload.WriteUInt8(player.flags);
        p.payload.WriteUInt32(player.lastProcessedInputTick);
        p.payload.WriteFloat(player.pingMs);
    }

    // Write objects
    p.payload.WriteUInt16(static_cast<uint16_t>(snapshot.objects.size()));
    for (const auto& obj : snapshot.objects) {
        p.payload.WriteUInt32(obj.networkId);
        p.payload.WriteUInt32(obj.objectType);
        p.payload.WriteString(obj.modelName);
        p.payload.WriteVector3(obj.position);
        p.payload.WriteVector3(obj.rotation);
        p.payload.WriteVector3(obj.scale);
        p.payload.WriteVector3(obj.linearVelocity);
        p.payload.WriteBool(obj.isStatic);
    }

    return p;
}

Packet Packet::CreateSpawnObject(const DynamicObjectStateData& obj) {
    Packet p(PacketType::SpawnObject, PacketChannel::Reliable);
    p.payload.WriteUInt32(obj.networkId);
    p.payload.WriteUInt32(obj.objectType);
    p.payload.WriteString(obj.modelName);
    p.payload.WriteVector3(obj.position);
    p.payload.WriteVector3(obj.rotation);
    p.payload.WriteVector3(obj.scale);
    p.payload.WriteVector3(obj.linearVelocity);
    p.payload.WriteBool(obj.isStatic);
    return p;
}

std::vector<Packet> Packet::CreateWorldSnapshotChunks(const WorldSnapshotData& snapshot) {
    const auto raw = CreateWorldSnapshot(snapshot).Serialize();
    std::vector<Packet> chunks;
    if (raw.empty()) return chunks;
    constexpr size_t chunkBytes = 1200;
    const auto count = static_cast<uint16_t>((raw.size() + chunkBytes - 1) / chunkBytes);
    for (uint16_t i = 0; i < count; ++i) {
        Packet p(PacketType::WorldSnapshotChunk);
        p.payload.WriteUInt32(snapshot.serverTick);
        p.payload.WriteUInt16(i);
        p.payload.WriteUInt16(count);
        p.payload.WriteUInt32(static_cast<uint32_t>(raw.size()));
        const size_t offset = i * chunkBytes;
        p.payload.WriteBytes(raw.data() + offset, (std::min)(chunkBytes, raw.size() - offset));
        chunks.push_back(std::move(p));
    }
    return chunks;
}

Packet Packet::CreateDestroyObject(uint32_t networkId) {
    Packet p(PacketType::DestroyObject, PacketChannel::Reliable);
    p.payload.WriteUInt32(networkId);
    return p;
}

Packet Packet::CreateChatMessage(uint32_t senderId, const std::string& senderName, const std::string& message) {
    Packet p(PacketType::ChatMessage, PacketChannel::Reliable);
    p.payload.WriteUInt32(senderId);
    p.payload.WriteString(senderName);
    p.payload.WriteString(message);
    return p;
}

Packet Packet::CreateServerCommand(const std::string& command) {
    Packet p(PacketType::ServerCommand, PacketChannel::Reliable);
    p.payload.WriteString(command);
    return p;
}

Packet Packet::CreateHeartbeat() {
    Packet p(PacketType::Heartbeat, PacketChannel::Unreliable);
    return p;
}

Packet Packet::CreateAntiCheatChallenge(uint32_t nonce) {
    Packet p(PacketType::AntiCheatChallenge, PacketChannel::Reliable);
    p.payload.WriteUInt32(nonce);
    return p;
}

Packet Packet::CreateAntiCheatResponse(uint32_t nonce, uint64_t codeHash, uint32_t securityFlags) {
    Packet p(PacketType::AntiCheatResponse, PacketChannel::Reliable);
    p.payload.WriteUInt32(nonce);
    p.payload.WriteUInt64(codeHash);
    p.payload.WriteUInt32(securityFlags);
    return p;
}

bool Packet::ReadConnectionRequest(BitReader& reader, std::string& outPlayerName, uint32_t& outVersion) {
    return reader.ReadString(outPlayerName) && reader.ReadUInt32(outVersion);
}

bool Packet::ReadConnectionAccepted(BitReader& reader, uint32_t& outClientId, uint32_t& outServerTick, float& outTickRate) {
    return reader.ReadUInt32(outClientId) && reader.ReadUInt32(outServerTick) && reader.ReadFloat(outTickRate);
}

bool Packet::ReadConnectionRejected(BitReader& reader, RejectReason& outReason, std::string& outMessage) {
    uint8_t r = 0;
    if (!reader.ReadUInt8(r)) return false;
    outReason = static_cast<RejectReason>(r);
    return reader.ReadString(outMessage);
}

bool Packet::ReadDisconnect(BitReader& reader, uint32_t& outClientId, std::string& outReason) {
    return reader.ReadUInt32(outClientId) && reader.ReadString(outReason);
}

bool Packet::ReadPing(BitReader& reader, uint32_t& outPingId, double& outTimestamp) {
    return reader.ReadUInt32(outPingId) && reader.ReadDouble(outTimestamp);
}

bool Packet::ReadPong(BitReader& reader, uint32_t& outPingId, double& outClientTimestamp, double& outServerTimestamp) {
    return reader.ReadUInt32(outPingId) && reader.ReadDouble(outClientTimestamp) && reader.ReadDouble(outServerTimestamp);
}

bool Packet::ReadPlayerInput(BitReader& reader, PlayerInputData& outInput) {
    return reader.ReadUInt32(outInput.inputTick) &&
           reader.ReadFloat(outInput.moveX) &&
           reader.ReadFloat(outInput.moveY) &&
           reader.ReadFloat(outInput.lookYaw) &&
           reader.ReadFloat(outInput.lookPitch) &&
           reader.ReadBool(outInput.jump) &&
           reader.ReadBool(outInput.crouch) &&
           reader.ReadBool(outInput.sprint) &&
           reader.ReadBool(outInput.fire) &&
           reader.ReadFloat(outInput.deltaTime);
}

bool Packet::ReadWorldSnapshot(BitReader& reader, WorldSnapshotData& outSnapshot) {
    if (!reader.ReadUInt32(outSnapshot.serverTick) ||
        !reader.ReadUInt32(outSnapshot.lastAckedInputTick) ||
        !reader.ReadDouble(outSnapshot.timestamp)) {
        return false;
    }

    uint16_t playerCount = 0;
    if (!reader.ReadUInt16(playerCount)) return false;

    outSnapshot.players.clear();
    outSnapshot.players.reserve(playerCount);
    for (uint16_t i = 0; i < playerCount; ++i) {
        PlayerStateData p;
        if (!reader.ReadUInt32(p.networkId) ||
            !reader.ReadUInt32(p.ownerClientId) ||
            !reader.ReadString(p.playerName) ||
            !reader.ReadVector3(p.position) ||
            !reader.ReadVector3(p.velocity) ||
            !reader.ReadFloat(p.yaw) ||
            !reader.ReadFloat(p.pitch) ||
            !reader.ReadFloat(p.health) ||
            !reader.ReadUInt8(p.flags) ||
            !reader.ReadUInt32(p.lastProcessedInputTick) ||
            !reader.ReadFloat(p.pingMs)) {
            return false;
        }
        outSnapshot.players.push_back(p);
    }

    uint16_t objCount = 0;
    if (!reader.ReadUInt16(objCount)) return false;

    outSnapshot.objects.clear();
    outSnapshot.objects.reserve(objCount);
    for (uint16_t i = 0; i < objCount; ++i) {
        DynamicObjectStateData obj;
        if (!reader.ReadUInt32(obj.networkId) ||
            !reader.ReadUInt32(obj.objectType) ||
            !reader.ReadString(obj.modelName) ||
            !reader.ReadVector3(obj.position) ||
            !reader.ReadVector3(obj.rotation) ||
            !reader.ReadVector3(obj.scale) ||
            !reader.ReadVector3(obj.linearVelocity) ||
            !reader.ReadBool(obj.isStatic)) {
            return false;
        }
        outSnapshot.objects.push_back(obj);
    }

    return true;
}

bool Packet::ReadSpawnObject(BitReader& reader, DynamicObjectStateData& outObj) {
    return reader.ReadUInt32(outObj.networkId) &&
           reader.ReadUInt32(outObj.objectType) &&
           reader.ReadString(outObj.modelName) &&
           reader.ReadVector3(outObj.position) &&
           reader.ReadVector3(outObj.rotation) &&
           reader.ReadVector3(outObj.scale) &&
           reader.ReadVector3(outObj.linearVelocity) &&
           reader.ReadBool(outObj.isStatic);
}

bool Packet::ReadDestroyObject(BitReader& reader, uint32_t& outNetworkId) {
    return reader.ReadUInt32(outNetworkId);
}

bool Packet::ReadChatMessage(BitReader& reader, uint32_t& outSenderId, std::string& outSenderName, std::string& outMessage) {
    return reader.ReadUInt32(outSenderId) && reader.ReadString(outSenderName) && reader.ReadString(outMessage);
}

bool Packet::ReadServerCommand(BitReader& reader, std::string& outCommand) {
    return reader.ReadString(outCommand);
}

bool Packet::ReadAntiCheatChallenge(BitReader& reader, uint32_t& outNonce) {
    return reader.ReadUInt32(outNonce);
}

bool Packet::ReadAntiCheatResponse(BitReader& reader, uint32_t& outNonce, uint64_t& outCodeHash, uint32_t& outSecurityFlags) {
    return reader.ReadUInt32(outNonce) && reader.ReadUInt64(outCodeHash) && reader.ReadUInt32(outSecurityFlags);
}

} // namespace EngineNetwork
