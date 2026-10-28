#pragma once

#include "NetworkTypes.h"
#include "NetworkSocket.h"
#include "NetworkConnection.h"
#include "Packet.h"
#include "NetworkReplication.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <functional>
#include <memory>

#include "ServerAntiCheat.h"

namespace EngineNetwork {

struct ServerConfig {
    uint16_t port = DEFAULT_PORT;
    uint32_t maxPlayers = 32;
    float tickRate = 60.0f;       // Server physics & game logic simulation frequency (Hz)
    float networkRate = 20.0f;    // World snapshot broadcast frequency (Hz)
    std::string serverName = "Idontknow Dedicated Server";
    std::string worldName = "world";
    TlsOptions tls;
};

struct ServerClient {
    uint32_t clientId = 0;
    uint32_t playerNetworkId = 0;
    NetworkConnection connection;
    PlayerStateData playerState;
    std::vector<PlayerInputData> pendingInputs;
    uint32_t lastProcessedInputTick = 0;
    PlayerInputData heldInput;
    double lastInputReceivedTime = -1.0;
    bool hasQueuedInput = false;
    uint32_t lastQueuedInputTick = 0;
    double lastChatTime=-1;
};

class NetworkServer {
public:
    NetworkServer();
    ~NetworkServer();

    bool Start(const ServerConfig& config);
    void Stop();
    bool IsRunning() const { return m_isRunning; }

    // Update server tick (process packets, simulate physics/players, send snapshots)
    void Update(double currentTime, double deltaTime);

    // Broadcast messages
    void BroadcastPacket(Packet& packet, bool reliable = true, uint32_t excludeClientId = 0);
    void SendToClient(uint32_t clientId, Packet& packet, bool reliable = true);

    // Administrative actions
    bool KickPlayer(uint32_t clientId, const std::string& reason = "Kicked by server admin");
    bool BanPlayer(uint32_t clientId, const std::string& reason = "Banned by server admin");
    bool BanIP(const std::string& ip);
    void BroadcastChat(const std::string& senderName, const std::string& message);

    ServerAntiCheat& GetAntiCheat() { return m_antiCheat; }
    const ServerAntiCheat& GetAntiCheat() const { return m_antiCheat; }

    // Status queries
    const ServerConfig& GetConfig() const { return m_config; }
    size_t GetConnectedPlayerCount() const { return m_clients.size(); }
    const std::unordered_map<uint32_t, ServerClient>& GetClients() const { return m_clients; }
    double GetUptimeSeconds() const { return m_uptime; }
    uint32_t GetCurrentServerTick() const { return m_serverTick; }

    // Callbacks
    using PlayerEventCallback = std::function<void(uint32_t clientId, const std::string& name, const NetworkAddress& address)>;
    using DisconnectCallback = std::function<void(uint32_t clientId, const std::string& reason)>;
    using ChatCallback = std::function<void(uint32_t clientId, const std::string& name, const std::string& message)>;
    using LogCallback = std::function<void(const std::string& log)>;

    void SetOnPlayerConnected(PlayerEventCallback cb) { m_onPlayerConnected = cb; }
    void SetOnPlayerDisconnected(DisconnectCallback cb) { m_onPlayerDisconnected = cb; }
    void SetOnChatMessage(ChatCallback cb) { m_onChatMessage = cb; }
    void SetOnLog(LogCallback cb) { m_onLog = cb; }

    // Scene collision box for server-side character simulation
    struct WorldCollisionBox {
        DirectX::XMFLOAT3 minPoint;
        DirectX::XMFLOAT3 maxPoint;
    };
    void SetWorldCollisionBoxes(const std::vector<WorldCollisionBox>& boxes) { m_collisionBoxes = boxes; }

private:
    void ProcessIncomingPackets(double currentTime);
    void ProcessPacket(const NetworkAddress& sender, const PacketHeader& header, BitReader& reader, double currentTime);
    
    void HandleConnectionRequest(const NetworkAddress& sender, const PacketHeader& header, BitReader& reader, double currentTime);
    void HandleDisconnect(const NetworkAddress& sender, const PacketHeader& header, BitReader& reader, double currentTime);
    void HandlePlayerInput(const NetworkAddress& sender, const PacketHeader& header, BitReader& reader, double currentTime);
    void HandlePing(const NetworkAddress& sender, const PacketHeader& header, BitReader& reader, double currentTime);
    void HandleChatMessage(const NetworkAddress& sender, const PacketHeader& header, BitReader& reader, double currentTime);
    void HandleAntiCheatResponse(const NetworkAddress& sender, const PacketHeader& header, BitReader& reader, double currentTime);

    void SimulatePlayers(float dt);
    void SimulateSinglePlayer(ServerClient& client, const PlayerInputData& input, float dt);
    bool CheckWorldCollision(const DirectX::XMFLOAT3& pos, float radius = 0.4f, float height = 1.8f) const;

    void BroadcastWorldSnapshot(double currentTime);
    void CheckClientTimeouts(double currentTime);
    void Log(const std::string& message);

    uint32_t AllocateClientId();
    ServerClient* FindClientByAddress(const NetworkAddress& address);
    ServerClient* FindClientById(uint32_t clientId);
    void FlushPendingKicks();
    std::unordered_map<uint32_t, std::string> m_pendingKicks;
    bool m_updating = false;

    ServerConfig m_config;
    struct JoinBudget {double start=0;unsigned count=0;};
    std::unordered_map<std::string,JoinBudget> m_joinBudgets;
    double m_lastJoinBudgetCleanup=-1;
    NetworkSocket m_socket;
    bool m_isRunning = false;

    uint32_t m_nextClientId = 1;
    uint32_t m_serverTick = 0;
    double m_uptime = 0.0;
    double m_lastSnapshotBroadcastTime = 0.0;

    std::unordered_map<uint32_t, ServerClient> m_clients;
    std::unordered_set<std::string> m_bannedIPs;
    std::vector<WorldCollisionBox> m_collisionBoxes;

    NetworkReplicationManager m_replicationManager;
    ServerAntiCheat m_antiCheat;

    PlayerEventCallback m_onPlayerConnected;
    DisconnectCallback  m_onPlayerDisconnected;
    ChatCallback        m_onChatMessage;
    LogCallback         m_onLog;
};

} // namespace EngineNetwork
