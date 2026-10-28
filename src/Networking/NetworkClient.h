#pragma once

#include "NetworkTypes.h"
#include "NetworkSocket.h"
#include "NetworkConnection.h"
#include "Packet.h"
#include "NetworkReplication.h"
#include "ClientAntiCheat.h"
#include <unordered_map>
#include <deque>
#include <string>
#include <functional>
#include <memory>

namespace EngineNetwork {

struct RemotePlayerInfo {
    uint32_t networkId = 0;
    uint32_t clientId = 0;
    std::string playerName = "Player";
    std::shared_ptr<NetworkObject> networkObject;
    PlayerStateData latestState;
    float pingMs = 0.0f;
};

class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();

    // Connect to server (supports "127.0.0.1" + 7777 or "127.0.0.1:7777")
    bool Connect(const std::string& addressStr, uint16_t port = DEFAULT_PORT, const std::string& playerName = "Player");
    void Disconnect(const std::string& reason = "Client Disconnect");
    void SetTlsOptions(const TlsOptions& options){m_tlsOptions=options;}
    const TlsOptions& GetTlsOptions()const{return m_tlsOptions;}

    ConnectionState GetState() const { return m_state; }
    bool IsConnected() const { return m_state == ConnectionState::Connected; }
    bool IsTlsActive()const{return IsConnected()&&m_socket.IsTls()&&m_socket.IsPeerConnected(m_serverConnection.GetAddress());}
    bool IsConnecting() const { return m_state == ConnectionState::Connecting; }

    uint32_t GetLocalClientId() const { return m_localClientId; }
    uint32_t GetLocalNetworkId() const { return m_localNetworkId; }
    float GetPingMs() const { return m_pingMs; }
    const std::string& GetServerAddressString() const { return m_serverAddressStr; }
    const std::string& GetDisconnectReason() const { return m_disconnectReason; }

    // Update client networking, process packets, perform prediction & interpolation
    void Update(double currentTime, double deltaTime);

    // Send local player input to server (with client prediction)
    void SendPlayerInput(const PlayerInputData& input, double currentTime);

    // Get predicted local player state (for camera and local physics)
    const PlayerStateData& GetPredictedLocalPlayerState() const { return m_predictedLocalState; }
    void SetPredictedPosition(const DirectX::XMFLOAT3& pos) { m_predictedLocalState.position = pos; }

    // Remote players access for rendering proxy avatars
    const std::unordered_map<uint32_t, RemotePlayerInfo>& GetRemotePlayers() const { return m_remotePlayers; }

    // Chat
    void SendChat(const std::string& message);

    ClientAntiCheat& GetAntiCheat() { return m_antiCheat; }
    const ClientAntiCheat& GetAntiCheat() const { return m_antiCheat; }

    // Callbacks
    using ConnectCallback = std::function<void(uint32_t clientId)>;
    using DisconnectCallback = std::function<void(const std::string& reason)>;
    using ChatCallback = std::function<void(uint32_t senderId, const std::string& senderName, const std::string& message)>;
    using LogCallback = std::function<void(const std::string& log)>;

    void SetOnConnected(ConnectCallback cb) { m_onConnected = cb; }
    void SetOnDisconnected(DisconnectCallback cb) { m_onDisconnected = cb; }
    void SetOnChatMessage(ChatCallback cb) { m_onChatMessage = cb; }
    void SetOnLog(LogCallback cb) { m_onLog = cb; }

    // Scene collision box for client prediction
    struct WorldCollisionBox {
        DirectX::XMFLOAT3 minPoint;
        DirectX::XMFLOAT3 maxPoint;
    };
    void SetWorldCollisionBoxes(const std::vector<WorldCollisionBox>& boxes) { m_collisionBoxes = boxes; }

private:
    void ProcessIncomingPackets(double currentTime);
    void ProcessPacket(const PacketHeader& header, BitReader& reader, double currentTime);

    void HandleConnectionAccepted(BitReader& reader, double currentTime);
    void HandleConnectionRejected(BitReader& reader, double currentTime);
    void HandleDisconnect(BitReader& reader, double currentTime);
    void HandleWorldSnapshot(BitReader& reader, double currentTime);
    void HandleWorldSnapshotChunk(BitReader& reader, double currentTime);
    struct SnapshotAssembly {
        double started = 0.0;
        uint32_t totalBytes = 0;
        std::vector<std::vector<uint8_t>> parts;
    };
    std::unordered_map<uint32_t, SnapshotAssembly> m_snapshotAssemblies;
    bool m_hasWorldSnapshot = false;
    void HandlePong(BitReader& reader, double currentTime);
    void HandleChatMessage(BitReader& reader, double currentTime);
    void HandleAntiCheatChallenge(BitReader& reader, double currentTime);

    void SendPing(double currentTime);
    void ReconcileServerState(const PlayerStateData& serverState);
    void PredictMovement(PlayerStateData& state, const PlayerInputData& input, float dt);
    bool CheckWorldCollision(const DirectX::XMFLOAT3& pos, float radius = 0.4f, float height = 1.8f) const;

    void Log(const std::string& message);

    TlsOptions m_tlsOptions;
    NetworkSocket m_socket;
    NetworkConnection m_serverConnection;
    ConnectionState m_state = ConnectionState::Disconnected;

    std::string m_serverAddressStr = "127.0.0.1:7777";
    std::string m_localPlayerName = "Player";
    std::string m_disconnectReason;

    uint32_t m_localClientId = 0;
    uint32_t m_localNetworkId = 0;
    uint32_t m_currentInputTick = 0;
    double m_lastInputSentTime = -1.0;
    double m_pendingInputTime = 0.0;
    bool m_pendingJump = false;
    bool m_pendingFire = false;
    uint32_t m_serverTick = 0;

    double m_currentTime = 0.0;
    double m_connectStartTime = 0.0;
    double m_lastPingSentTime = 0.0;
    uint32_t m_nextPingId = 1;
    float m_pingMs = 0.0f;

    // Client-side prediction buffer
    PlayerStateData m_predictedLocalState;
    std::deque<PlayerInputData> m_unacknowledgedInputs;

    // Remote players registry
    std::unordered_map<uint32_t, RemotePlayerInfo> m_remotePlayers;
    std::vector<WorldCollisionBox> m_collisionBoxes;
    ClientAntiCheat m_antiCheat;

    ConnectCallback    m_onConnected;
    DisconnectCallback m_onDisconnected;
    ChatCallback       m_onChatMessage;
    LogCallback        m_onLog;
};

} // namespace EngineNetwork
