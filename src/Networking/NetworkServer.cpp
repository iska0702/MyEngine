#include "NetworkServer.h"
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace EngineNetwork {
static std::string CleanNetworkText(std::string text){
    for(char& c:text)if(static_cast<unsigned char>(c)<32||static_cast<unsigned char>(c)==127)c=' ';
    auto first=text.find_first_not_of(' ');if(first==std::string::npos)return {};
    return text.substr(first,text.find_last_not_of(' ')-first+1);
}


NetworkServer::NetworkServer() {}

NetworkServer::~NetworkServer() {
    Stop();
}

bool NetworkServer::Start(const ServerConfig& config) {
    Stop();
    m_config = config;
    m_config.tickRate=std::isfinite(m_config.tickRate)?(std::clamp)(m_config.tickRate,15.f,240.f):60.f;
    m_config.networkRate=std::isfinite(m_config.networkRate)?(std::clamp)(m_config.networkRate,1.f,60.f):20.f;
    m_config.maxPlayers = (std::clamp)(m_config.maxPlayers, 1u, static_cast<uint32_t>(MAX_CLIENTS));

    if (!(m_config.tls.enabled ? m_socket.OpenTls(m_config.tls,true,m_config.port) : m_socket.Open(m_config.port, true))) {
        Log("ERROR: "+m_socket.GetError()+" Failed to bind server socket on port " + std::to_string(m_config.port));
        return false;
    }

    m_config.port=m_socket.GetBoundPort();m_joinBudgets.clear();m_lastJoinBudgetCleanup=-1;
    Log(m_config.tls.enabled?"Transport: QUIC / TLS 1.3 (authenticated, encrypted)":"Transport: legacy UDP (unencrypted)");
    m_isRunning = true;
    m_serverTick = 0;
    m_uptime = 0.0;
    m_lastSnapshotBroadcastTime = 0.0;
    m_clients.clear();
    m_pendingKicks.clear();
    m_updating = false;
    m_replicationManager.Clear();
    m_antiCheat.Initialize(this);

    Log("========================================");
    Log("       " + m_config.serverName);
    Log("========================================");
    Log("Port: " + std::to_string(m_config.port));
    Log("Max Players: " + std::to_string(m_config.maxPlayers));
    Log("Tick Rate: " + std::to_string(static_cast<int>(m_config.tickRate)) + " Hz");
    Log("Network Rate: " + std::to_string(static_cast<int>(m_config.networkRate)) + " Hz");
    Log("World / Map: " + m_config.worldName);
    Log("Server started successfully.");
    Log("Waiting for connections...");
    return true;
}

void NetworkServer::Stop() {
    if (!m_isRunning) return;

    // Notify all clients of server shutdown
    for (auto& [id, client] : m_clients) {
        Packet dc = Packet::CreateDisconnect(id, "Server Shutting Down");
        client.connection.SendReliable(m_socket, dc, m_uptime);
    }

    m_socket.Close();
    m_clients.clear();
    m_replicationManager.Clear();
    m_isRunning = false;
    Log("Server stopped.");
}

void NetworkServer::Log(const std::string& message) {
    if (m_onLog) {
        m_onLog(message);
    } else {
        std::cout << message << std::endl;
    }
}

uint32_t NetworkServer::AllocateClientId() {
    return m_nextClientId++;
}

ServerClient* NetworkServer::FindClientByAddress(const NetworkAddress& address) {
    for (auto& [id, client] : m_clients) {
        if (client.connection.GetAddress() == address) {
            return &client;
        }
    }
    return nullptr;
}

ServerClient* NetworkServer::FindClientById(uint32_t clientId) {
    auto it = m_clients.find(clientId);
    if (it != m_clients.end()) {
        return &it->second;
    }
    return nullptr;
}

void NetworkServer::Update(double currentTime, double deltaTime) {
    if (!m_isRunning || !std::isfinite(currentTime) || !std::isfinite(deltaTime) ||
        currentTime < m_uptime || deltaTime <= 0.0) return;
    deltaTime = (std::min)(deltaTime, 0.1);

    m_uptime = currentTime;
    m_updating = true;
    m_serverTick++;

    if(m_lastJoinBudgetCleanup<0||currentTime-m_lastJoinBudgetCleanup>=1){
        m_lastJoinBudgetCleanup=currentTime;
        for(auto it=m_joinBudgets.begin();it!=m_joinBudgets.end();)if(currentTime-it->second.start>10)it=m_joinBudgets.erase(it);else ++it;
    }
    // 1. Process incoming packets
    ProcessIncomingPackets(currentTime);

    // 2. Integrity heartbeat telemetry and response deadlines
    m_antiCheat.Update(currentTime);
    FlushPendingKicks();

    // 3. Simulate server authoritative physics and player inputs at tick rate
    SimulatePlayers(static_cast<float>(deltaTime));
    FlushPendingKicks();

    // 3. Update connection reliability queues and heartbeats
    for (auto& [id, client] : m_clients) {
        client.connection.Update(m_socket, currentTime, deltaTime);
    }

    // 4. Check for client timeouts
    CheckClientTimeouts(currentTime);

    // 5. Broadcast world snapshot at configured network rate
    double snapshotInterval = 1.0 / (std::max)(m_config.networkRate, 1.0f);
    if (currentTime - m_lastSnapshotBroadcastTime >= snapshotInterval) {
        m_lastSnapshotBroadcastTime = currentTime;
        BroadcastWorldSnapshot(currentTime);
    }
    m_updating = false;
}

void NetworkServer::ProcessIncomingPackets(double currentTime) {
    uint8_t buffer[MAX_PACKET_SIZE];
    NetworkAddress sender;
    size_t bytesRead = 0;

    // Bound receive work so a continuously readable UDP socket cannot starve simulation.
    for (unsigned received = 0; received < 1024 && m_socket.Receive(sender, buffer, sizeof(buffer), bytesRead); ++received) {
        if (m_bannedIPs.count(sender.ip)){m_socket.ClosePeer(sender);continue;}
        if (auto* client = FindClientByAddress(sender)) {
            if (m_pendingKicks.count(client->clientId) ||
                !m_antiCheat.ValidatePacketRate(client->clientId, currentTime)) continue;
        }
        if (bytesRead < sizeof(PacketHeader)) continue;

        PacketHeader header;
        BitReader reader;
        if (!Packet::Deserialize(buffer, bytesRead, header, reader)) {
            continue;
        }

        ProcessPacket(sender, header, reader, currentTime);
    }
}

void NetworkServer::ProcessPacket(const NetworkAddress& sender, const PacketHeader& header, BitReader& reader, double currentTime) {
    PacketType type = static_cast<PacketType>(header.packetType);

    if (type == PacketType::ConnectionRequest) {
        HandleConnectionRequest(sender, header, reader, currentTime);
        return;
    }

    ServerClient* client = FindClientByAddress(sender);
    if (!client) {
        // Unknown client packet -> ignore or request reconnect
        return;
    }

    if (!client->connection.ProcessIncomingHeader(header, currentTime)) return;

    switch (type) {
        case PacketType::Disconnect:
            HandleDisconnect(sender, header, reader, currentTime);
            break;
        case PacketType::PlayerInput:
            HandlePlayerInput(sender, header, reader, currentTime);
            break;
        case PacketType::Ping:
            HandlePing(sender, header, reader, currentTime);
            break;
        case PacketType::ChatMessage:
            HandleChatMessage(sender, header, reader, currentTime);
            break;
        case PacketType::AntiCheatResponse:
            HandleAntiCheatResponse(sender, header, reader, currentTime);
            break;
        case PacketType::Heartbeat:
            // Heartbeat just refreshes m_lastReceivedTime in ProcessIncomingHeader
            break;
        default:
            break;
    }
}

void NetworkServer::HandleConnectionRequest(const NetworkAddress& sender, const PacketHeader& header, BitReader& reader, double currentTime) {
    if(!FindClientByAddress(sender)){
        if(!m_joinBudgets.count(sender.ip)&&m_joinBudgets.size()>=4096)return;
        auto& budget=m_joinBudgets[sender.ip];if(currentTime-budget.start>=10)budget={currentTime,0};if(budget.count>=64)return;++budget.count;if(budget.count==1)budget.start=currentTime;
    }
    std::string playerName;
    uint32_t clientVersion = 0;
    if (!Packet::ReadConnectionRequest(reader, playerName, clientVersion)) {
        return;
    }
    if (playerName.size() > 64 || clientVersion != 2) return;
    playerName=CleanNetworkText(playerName);
    if (auto* existing = FindClientByAddress(sender)) {
        existing->connection.ProcessIncomingHeader(header, currentTime);
        Packet accept = Packet::CreateConnectionAccepted(existing->clientId, m_serverTick, m_config.tickRate);
        existing->connection.SendReliable(m_socket, accept, currentTime);
        return;
    }

    // Check if server is full
    if (m_clients.size() >= m_config.maxPlayers) {
        Packet reject = Packet::CreateConnectionRejected(RejectReason::ServerFull, "Server is full.");
        std::vector<uint8_t> raw = reject.Serialize();
        m_socket.Send(sender, raw.data(), raw.size());
        Log("Connection rejected for " + sender.ToString() + ": Server Full");
        return;
    }

    // If client was already connected from same address, recycle
    ServerClient* existing = FindClientByAddress(sender);
    if (existing) {
        m_clients.erase(existing->clientId);
    }

    uint32_t newId = AllocateClientId();
    ServerClient newClient;
    newClient.clientId = newId;
    newClient.playerNetworkId = m_replicationManager.AllocateNetworkId();
    newClient.connection = NetworkConnection(sender, newId);
    newClient.connection.SetPlayerName(playerName);
    newClient.connection.SetState(ConnectionState::Connected);

    // Initial spawn position spread in circle around center
    float angle = (newId * 0.8f);
    float spawnDist = 2.5f + (newId % 3) * 1.5f;
    newClient.playerState.networkId = newClient.playerNetworkId;
    newClient.playerState.ownerClientId = newId;
    newClient.playerState.playerName = playerName.empty() ? ("Player " + std::to_string(newId)) : playerName;
    newClient.playerState.position = DirectX::XMFLOAT3(std::cos(angle) * spawnDist, 1.0f, std::sin(angle) * spawnDist);
    newClient.playerState.velocity = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
    newClient.playerState.yaw = 0.0f;
    newClient.playerState.pitch = 0.0f;
    newClient.playerState.health = 100.0f;
    newClient.playerState.flags = PlayerFlags::IsGrounded | PlayerFlags::IsAlive;
    newClient.playerState.lastProcessedInputTick = 0;
    newClient.playerState.pingMs = 0.0f;

    m_socket.AcceptPeer(sender);
    m_clients[newId] = std::move(newClient);

    // Send ConnectionAccepted
    ServerClient& insertedClient = m_clients[newId];
    insertedClient.connection.ProcessIncomingHeader(header, currentTime);

    // Anti-Cheat: Register client tracking state
    m_antiCheat.RegisterClient(newId, insertedClient.playerState.playerName, sender.ip, insertedClient.playerState.position);

    Packet accept = Packet::CreateConnectionAccepted(newId, m_serverTick, m_config.tickRate);
    insertedClient.connection.SendReliable(m_socket, accept, currentTime);

    Log("========================================");
    Log("Player connected:");
    Log("  ID:   " + std::to_string(newId));
    Log("  Name: " + insertedClient.playerState.playerName);
    Log("  IP:   " + sender.ToString());
    Log("  Total Players: " + std::to_string(m_clients.size()) + "/" + std::to_string(m_config.maxPlayers));
    Log("========================================");

    if (m_onPlayerConnected) {
        m_onPlayerConnected(newId, insertedClient.playerState.playerName, sender);
    }
}

void NetworkServer::HandleDisconnect(const NetworkAddress& sender, const PacketHeader& header, BitReader& reader, double currentTime) {
    uint32_t clientId = 0;
    std::string reason;
    if(!Packet::ReadDisconnect(reader, clientId, reason))return;
    reason=CleanNetworkText(reason);

    ServerClient* client = FindClientByAddress(sender);
    if (!client) return;

    uint32_t id = client->clientId;
    std::string name = client->playerState.playerName;

    Log("Player disconnected: ID: " + std::to_string(id) + " (" + name + "), Reason: " + reason);

    if (m_onPlayerDisconnected) {
        m_onPlayerDisconnected(id, reason);
    }

    m_antiCheat.UnregisterClient(id);
    m_socket.ClosePeer(client->connection.GetAddress());
    m_clients.erase(id);
}

void NetworkServer::HandlePlayerInput(const NetworkAddress& sender, const PacketHeader& header, BitReader& reader, double currentTime) {
    ServerClient* client = FindClientByAddress(sender);
    if (!client) return;

    PlayerInputData input;
    if (Packet::ReadPlayerInput(reader, input)) {
        if (!std::isfinite(input.moveX) || !std::isfinite(input.moveY) ||
            !std::isfinite(input.lookYaw) || !std::isfinite(input.lookPitch) ||
            !std::isfinite(input.deltaTime) || input.deltaTime <= 0.0f || input.deltaTime > 0.1f ||
            std::abs(input.moveX) > 1.0f || std::abs(input.moveY) > 1.0f ||
            std::abs(input.lookYaw) > 100000.0f || std::abs(input.lookPitch) > 3.141593f) return;
        if (client->hasQueuedInput && static_cast<int32_t>(input.inputTick - client->lastQueuedInputTick) <= 0) return;
        if (client->pendingInputs.size() >= 120) return;
        client->hasQueuedInput = true;
        client->lastQueuedInputTick = input.inputTick;
        client->pendingInputs.push_back(input);
        client->lastInputReceivedTime = currentTime;
    }
}

void NetworkServer::HandlePing(const NetworkAddress& sender, const PacketHeader& header, BitReader& reader, double currentTime) {
    ServerClient* client = FindClientByAddress(sender);
    if (!client) return;

    uint32_t pingId = 0;
    double clientTime = 0.0;
    if (Packet::ReadPing(reader, pingId, clientTime)) {
        Packet pong = Packet::CreatePong(pingId, clientTime, currentTime);
        client->connection.SendUnreliable(m_socket, pong, currentTime);
    }
}

void NetworkServer::HandleChatMessage(const NetworkAddress& sender, const PacketHeader& header, BitReader& reader, double currentTime) {
    ServerClient* client = FindClientByAddress(sender);
    if (!client) return;

    uint32_t senderId = 0;
    std::string senderName, message;
    if (Packet::ReadChatMessage(reader, senderId, senderName, message)) {
        if(message.empty()||message.size()>512||(client->lastChatTime>=0&&currentTime-client->lastChatTime<.2))return;
        message=CleanNetworkText(message);if(message.empty())return;client->lastChatTime=currentTime;
        Log("[CHAT] " + client->playerState.playerName + ": " + message);
        
        // Broadcast to all clients
        Packet chatPacket = Packet::CreateChatMessage(client->clientId, client->playerState.playerName, message);
        BroadcastPacket(chatPacket, true);

        if (m_onChatMessage) {
            m_onChatMessage(client->clientId, client->playerState.playerName, message);
        }
    }
}

void NetworkServer::HandleAntiCheatResponse(const NetworkAddress& sender, const PacketHeader& header, BitReader& reader, double currentTime) {
    ServerClient* client = FindClientByAddress(sender);
    if (!client) return;

    uint32_t nonce = 0;
    uint64_t codeHash = 0;
    uint32_t securityFlags = 0;
    if (Packet::ReadAntiCheatResponse(reader, nonce, codeHash, securityFlags)) {
        m_antiCheat.HandleIntegrityResponse(client->clientId, nonce, codeHash, securityFlags, currentTime);
    }
}

bool NetworkServer::CheckWorldCollision(const DirectX::XMFLOAT3& pos, float radius, float height) const {
    DirectX::XMFLOAT3 boxMin(pos.x - radius, pos.y, pos.z - radius);
    DirectX::XMFLOAT3 boxMax(pos.x + radius, pos.y + height, pos.z + radius);

    for (const auto& box : m_collisionBoxes) {
        bool overlapX = (boxMin.x <= box.maxPoint.x) && (boxMax.x >= box.minPoint.x);
        bool overlapY = (boxMin.y <= box.maxPoint.y) && (boxMax.y >= box.minPoint.y);
        bool overlapZ = (boxMin.z <= box.maxPoint.z) && (boxMax.z >= box.minPoint.z);
        if (overlapX && overlapY && overlapZ) {
            return true;
        }
    }
    return false;
}

void NetworkServer::SimulateSinglePlayer(ServerClient& client, const PlayerInputData& input, float dt) {
    using namespace DirectX;
    if (m_pendingKicks.count(client.clientId)) return;

    // Anti-Cheat: Validate weapon fire rate and combat cooldowns
    const bool fireAllowed = m_antiCheat.ValidateCombatAction(client.clientId, input, m_uptime);
    if (!fireAllowed) {
        if (m_pendingKicks.count(client.clientId)) return;
    }

    // Apply look angles
    client.playerState.yaw = input.lookYaw;
    client.playerState.pitch = input.lookPitch;

    // Speed modifiers matching camera physics
    float baseSpeed = 2.5f;
    if (input.crouch) baseSpeed *= 0.4f;
    else if (input.sprint) baseSpeed *= 2.2f;

    // Compute forward/right vectors based on Yaw
    float sinY = std::sin(input.lookYaw);
    float cosY = std::cos(input.lookYaw);
    XMFLOAT3 forward(sinY, 0.0f, cosY);
    XMFLOAT3 right(cosY, 0.0f, -sinY);

    // Clamp the input vector as a whole: diagonal movement must not grant extra speed.
    const float inputLength = std::sqrt(input.moveX * input.moveX + input.moveY * input.moveY);
    const float inputScale = inputLength > 1.0f ? 1.0f / inputLength : 1.0f;
    // Compute horizontal desired movement
    float desiredMoveX = (forward.x * input.moveY + right.x * input.moveX) * baseSpeed * inputScale;
    float desiredMoveZ = (forward.z * input.moveY + right.z * input.moveX) * baseSpeed * inputScale;

    // Velocity update
    client.playerState.velocity.x = desiredMoveX;
    client.playerState.velocity.z = desiredMoveZ;

    // Gravity
    constexpr float gravity = -20.0f;
    client.playerState.velocity.y += gravity * dt;

    // Jump handling
    bool isGrounded = (client.playerState.flags & PlayerFlags::IsGrounded) != 0;
    if (input.jump && isGrounded) {
        client.playerState.velocity.y = 6.0f;
        isGrounded = false;
    }

    // Ground contact must be established again after every simulation step.
    isGrounded = false;

    // Step position
    XMFLOAT3 newPos = client.playerState.position;
    newPos.x += client.playerState.velocity.x * dt;
    newPos.z += client.playerState.velocity.z * dt;
    newPos.y += client.playerState.velocity.y * dt;

    // Collision with ground plane (floor at y = 1.0f eye-level / feet at 0.0f)
    if (newPos.y <= 1.0f) {
        newPos.y = 1.0f;
        client.playerState.velocity.y = 0.0f;
        isGrounded = true;
    }

    // Collision with scene obstacles
    if (!CheckWorldCollision(newPos)) {
        client.playerState.position = newPos;
    } else {
        // Slide along axes
        XMFLOAT3 tryX = client.playerState.position;
        tryX.x = newPos.x;
        if (!CheckWorldCollision(tryX)) client.playerState.position.x = newPos.x;

        XMFLOAT3 tryZ = client.playerState.position;
        tryZ.z = newPos.z;
        if (!CheckWorldCollision(tryZ)) client.playerState.position.z = newPos.z;

        XMFLOAT3 tryY = client.playerState.position;
        tryY.y = newPos.y;
        if (!CheckWorldCollision(tryY)) client.playerState.position.y = newPos.y;
        else {
            if (client.playerState.velocity.y <= 0.0f) isGrounded = true;
            client.playerState.velocity.y = 0.0f;
        }
    }

    // Update state flags
    uint8_t flags = PlayerFlags::IsAlive;
    if (isGrounded) flags |= PlayerFlags::IsGrounded;
    if (input.crouch) flags |= PlayerFlags::IsCrouching;
    if (input.sprint) flags |= PlayerFlags::IsSprinting;
    if (input.fire && fireAllowed) flags |= PlayerFlags::IsFiring;

    // Anti-Cheat: Authoritative speedhack, flyhack, teleportation verification
    m_antiCheat.ValidateMovement(client.clientId, input, dt, client.playerState.position, client.playerState.velocity, flags);
    client.playerState.flags = flags;

    client.lastProcessedInputTick = input.inputTick;
    client.playerState.lastProcessedInputTick = input.inputTick;
}

void NetworkServer::SimulatePlayers(float dt) {
    for (auto& [id, client] : m_clients) {
        PlayerInputData command;
        command.lookYaw = client.playerState.yaw;
        command.lookPitch = client.playerState.pitch;
        command.inputTick = client.lastProcessedInputTick;
        if (!client.pendingInputs.empty()) {
            // Input describes intent. Only the server tick determines elapsed simulation time.
            command = client.pendingInputs.back();
            client.heldInput = command;
            for (const auto& input : client.pendingInputs) {
                command.jump = command.jump || input.jump;
                command.fire = command.fire || input.fire;
            }
        } else if (client.lastInputReceivedTime >= 0.0 && m_uptime - client.lastInputReceivedTime <= .25) {
            command = client.heldInput;
            command.inputTick = client.lastProcessedInputTick;
        }
        command.deltaTime = dt;
        SimulateSinglePlayer(client, command, dt);
        client.pendingInputs.clear();
        client.playerState.pingMs = client.connection.GetPingMs();
    }
}

void NetworkServer::BroadcastWorldSnapshot(double currentTime) {
    if (m_clients.empty()) return;

    WorldSnapshotData snapshot;
    snapshot.serverTick = m_serverTick;
    snapshot.timestamp = currentTime;

    snapshot.players.reserve(m_clients.size());
    for (const auto& [id, client] : m_clients) {
        snapshot.players.push_back(client.playerState);
    }

    for (auto& [id, client] : m_clients) {
        snapshot.lastAckedInputTick = client.lastProcessedInputTick;
        for (auto& p : Packet::CreateWorldSnapshotChunks(snapshot)) {
            client.connection.SendUnreliable(m_socket, p, currentTime);
        }
    }
}

void NetworkServer::CheckClientTimeouts(double currentTime) {
    std::vector<uint32_t> timedOutClients;
    for (const auto& [id, client] : m_clients) {
        if ((m_socket.IsTls()&&!m_socket.IsPeerConnected(client.connection.GetAddress()))||client.connection.IsTimedOut(currentTime)) {
            timedOutClients.push_back(id);
        }
    }

    for (uint32_t id : timedOutClients) {
        auto it = m_clients.find(id);
        if (it != m_clients.end()) {
            Log("Player timed out: ID: " + std::to_string(id) + " (" + it->second.playerState.playerName + ")");
            if (m_onPlayerDisconnected) {
                m_onPlayerDisconnected(id, "Connection Timeout");
            }
            m_socket.ClosePeer(it->second.connection.GetAddress());
            m_clients.erase(it);
            m_antiCheat.UnregisterClient(id);
        }
    }
}

void NetworkServer::BroadcastPacket(Packet& packet, bool reliable, uint32_t excludeClientId) {
    for (auto& [id, client] : m_clients) {
        if (id == excludeClientId) continue;
        if (reliable) {
            client.connection.SendReliable(m_socket, packet, m_uptime);
        } else {
            client.connection.SendUnreliable(m_socket, packet, m_uptime);
        }
    }
}

void NetworkServer::SendToClient(uint32_t clientId, Packet& packet, bool reliable) {
    ServerClient* client = FindClientById(clientId);
    if (!client) return;

    if (reliable) {
        client->connection.SendReliable(m_socket, packet, m_uptime);
    } else {
        client->connection.SendUnreliable(m_socket, packet, m_uptime);
    }
}

bool NetworkServer::KickPlayer(uint32_t clientId, const std::string& reason) {
    ServerClient* client = FindClientById(clientId);
    if (!client) return false;
    if (m_updating) {
        m_pendingKicks[clientId] = reason;
        return true;
    }

    Packet dc = Packet::CreateDisconnect(clientId, reason);
    client->connection.SendReliable(m_socket, dc, m_uptime);

    Log("Kicking player ID " + std::to_string(clientId) + ": " + reason);
    if (m_onPlayerDisconnected) {
        m_onPlayerDisconnected(clientId, reason);
    }

    m_antiCheat.UnregisterClient(clientId);
    m_socket.ClosePeer(client->connection.GetAddress());
    m_clients.erase(clientId);
    return true;
}

bool NetworkServer::BanPlayer(uint32_t clientId, const std::string& reason) {
    ServerClient* client = FindClientById(clientId);
    if (!client) return false;

    std::string ip = client->connection.GetAddress().ip;
    m_bannedIPs.insert(ip);
    Log("Banning player ID " + std::to_string(clientId) + " (IP: " + ip + "): " + reason);

    KickPlayer(clientId, "Banned: " + reason);
    return true;
}

void NetworkServer::FlushPendingKicks() {
    auto pending = std::move(m_pendingKicks);
    m_pendingKicks.clear();
    m_updating = false;
    for (const auto& entry : pending) KickPlayer(entry.first, entry.second);
    m_updating = true;
}

bool NetworkServer::BanIP(const std::string& ip) {
    m_bannedIPs.insert(ip);
    Log("Banned IP: " + ip);
    return true;
}

void NetworkServer::BroadcastChat(const std::string& senderName, const std::string& message) {
    Packet p = Packet::CreateChatMessage(0, senderName, message);
    BroadcastPacket(p, true);
}

} // namespace EngineNetwork
