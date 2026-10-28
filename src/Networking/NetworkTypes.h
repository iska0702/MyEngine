#pragma once

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <DirectXMath.h>
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <signal.h>
    #include <cstring>

    using SOCKET = int;
    constexpr int INVALID_SOCKET = -1;
    constexpr int SOCKET_ERROR = -1;

    #include "Platform/DirectXMath/DirectXMath.h"
#endif

#include <cstdint>
#include <string>
#include <vector>

namespace EngineNetwork {

// Magic protocol identifier: "MYEN" (Idontknow Network)
constexpr uint32_t PROTOCOL_ID = 0x4D594532; // Protocol v2: snapshot fragmentation and input validation.
constexpr uint16_t DEFAULT_PORT = 7777;
constexpr size_t MAX_PACKET_SIZE = 1400; // Under typical MTU to avoid IP fragmentation
constexpr size_t MAX_CLIENTS = 64;
constexpr float CONNECTION_TIMEOUT_SECONDS = 5.0f;
constexpr float HEARTBEAT_INTERVAL_SECONDS = 0.5f;

enum class ApplicationMode {
    Client,
    DedicatedServer
};

enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Disconnecting,
    Failed,
    Timeout
};

enum class PacketType : uint8_t {
    None = 0,
    ConnectionRequest = 1,
    ConnectionAccepted = 2,
    ConnectionRejected = 3,
    Disconnect = 4,
    Ping = 5,
    Pong = 6,
    PlayerInput = 7,
    WorldSnapshot = 8,
    SpawnObject = 9,
    DestroyObject = 10,
    ChatMessage = 11,
    ServerCommand = 12,
    ServerStatus = 13,
    Heartbeat = 14,
    AntiCheatChallenge = 15,
    AntiCheatResponse = 16,
    WorldSnapshotChunk = 17
};

enum class PacketChannel : uint8_t {
    Unreliable = 0,
    Reliable = 1
};

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t protocolId = PROTOCOL_ID;
    uint16_t sequence = 0;
    uint16_t ack = 0;
    uint32_t ackBitfield = 0;
    uint8_t  packetType = 0;
    uint8_t  channel = 0;
    uint16_t payloadSize = 0;
};
#pragma pack(pop)

struct NetworkAddress {
    uint64_t transportId = 0; // Authenticated QUIC session identity; never supplied by game packets.
    std::string ip = "127.0.0.1";
    uint16_t port = DEFAULT_PORT;
    sockaddr_in socketAddress = {};

    NetworkAddress() {
        socketAddress.sin_family = AF_INET;
        socketAddress.sin_port = htons(DEFAULT_PORT);
        inet_pton(AF_INET, "127.0.0.1", &socketAddress.sin_addr);
    }

    NetworkAddress(const std::string& ipAddr, uint16_t p) : ip(ipAddr), port(p) {
        socketAddress.sin_family = AF_INET;
        socketAddress.sin_port = htons(p);
        inet_pton(AF_INET, ipAddr.c_str(), &socketAddress.sin_addr);
    }

    NetworkAddress(const sockaddr_in& addr) : socketAddress(addr) {
        char buf[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &addr.sin_addr, buf, INET_ADDRSTRLEN);
        ip = buf;
        port = ntohs(addr.sin_port);
    }

    std::string ToString() const {
        return ip + ":" + std::to_string(port);
    }

    bool operator==(const NetworkAddress& other) const {
        return transportId == other.transportId && socketAddress.sin_port == other.socketAddress.sin_port &&
               socketAddress.sin_addr.s_addr == other.socketAddress.sin_addr.s_addr;
    }

    bool operator<(const NetworkAddress& other) const {
        if(transportId!=other.transportId)return transportId<other.transportId;
        if (socketAddress.sin_addr.s_addr != other.socketAddress.sin_addr.s_addr)
            return socketAddress.sin_addr.s_addr < other.socketAddress.sin_addr.s_addr;
        return socketAddress.sin_port < other.socketAddress.sin_port;
    }
};

// Player Input sent from Client to Server
struct PlayerInputData {
    uint32_t inputTick = 0;
    float moveX = 0.0f;       // -1.0f (left) to 1.0f (right)
    float moveY = 0.0f;       // -1.0f (backward) to 1.0f (forward)
    float lookYaw = 0.0f;     // Camera Yaw in radians
    float lookPitch = 0.0f;   // Camera Pitch in radians
    bool jump = false;
    bool crouch = false;
    bool sprint = false;
    bool fire = false;
    float deltaTime = 0.0166f;
};

// State flags for player status
namespace PlayerFlags {
    constexpr uint8_t IsGrounded  = 1 << 0;
    constexpr uint8_t IsCrouching = 1 << 1;
    constexpr uint8_t IsSprinting = 1 << 2;
    constexpr uint8_t IsFiring    = 1 << 3;
    constexpr uint8_t IsAlive     = 1 << 4;
}

// Replicated Player State
struct PlayerStateData {
    uint32_t networkId = 0;
    uint32_t ownerClientId = 0;
    std::string playerName = "Player";
    DirectX::XMFLOAT3 position = { 0.0f, 1.0f, 0.0f };
    DirectX::XMFLOAT3 velocity = { 0.0f, 0.0f, 0.0f };
    float yaw = 0.0f;
    float pitch = 0.0f;
    float health = 100.0f;
    uint8_t flags = PlayerFlags::IsGrounded | PlayerFlags::IsAlive;
    uint32_t lastProcessedInputTick = 0;
    float pingMs = 0.0f;
};

// Replicated Generic Dynamic Object
struct DynamicObjectStateData {
    uint32_t networkId = 0;
    uint32_t objectType = 0; // 0 = Generic Cube/Box, 1 = Sphere/Projectile, 2 = Custom Model
    std::string modelName = "Cube";
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 rotation = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 scale = { 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 linearVelocity = { 0.0f, 0.0f, 0.0f };
    bool isStatic = false;
};

// World Snapshot broadcasted from Server to Clients
struct WorldSnapshotData {
    uint32_t serverTick = 0;
    uint32_t lastAckedInputTick = 0;
    double timestamp = 0.0;
    std::vector<PlayerStateData> players;
    std::vector<DynamicObjectStateData> objects;
};

// Connection reject reason
enum class RejectReason : uint8_t {
    ServerFull = 1,
    VersionMismatch = 2,
    Banned = 3,
    InvalidName = 4,
    Unknown = 5,
    AntiCheatViolation = 6,
    SpeedhackDetected = 7,
    FlyhackDetected = 8,
    TamperedBinary = 9
};

} // namespace EngineNetwork
