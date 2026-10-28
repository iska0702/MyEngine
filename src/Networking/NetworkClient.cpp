#include "NetworkClient.h"
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <unordered_set>

namespace EngineNetwork {

NetworkClient::NetworkClient() {}

NetworkClient::~NetworkClient() {
    Disconnect();
}

static bool ParseIpAndPort(const std::string& input, uint16_t defaultPort, std::string& outIp, uint16_t& outPort) {
    std::string host=input;outPort=defaultPort;
    size_t colon=input.find(':');
    if(colon!=std::string::npos){
        host=input.substr(0,colon);auto text=input.substr(colon+1);size_t consumed=0;
        try{unsigned long value=std::stoul(text,&consumed);if(consumed!=text.size()||value<1||value>65535)return false;outPort=uint16_t(value);}catch(...){return false;}
    }
    if(host.empty()||outPort==0)return false;
    addrinfo hints{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_DGRAM;
    addrinfo* addresses=nullptr;
    if(getaddrinfo(host.c_str(),nullptr,&hints,&addresses)!=0||!addresses)return false;
    char buffer[INET_ADDRSTRLEN]{};inet_ntop(AF_INET,&reinterpret_cast<sockaddr_in*>(addresses->ai_addr)->sin_addr,buffer,sizeof(buffer));
    outIp=buffer;freeaddrinfo(addresses);
    return true;
}

bool NetworkClient::Connect(const std::string& addressStr, uint16_t port, const std::string& playerName) {
    Disconnect();

    std::string ip;
    uint16_t targetPort = port;
    if(!NetworkSocket::GlobalInitialize()||!ParseIpAndPort(addressStr, port, ip, targetPort)){m_state=ConnectionState::Failed;m_disconnectReason="Invalid server address or port";return false;}

    m_serverAddressStr = ip + ":" + std::to_string(targetPort);
    m_localPlayerName = playerName.empty() ? "Player" : playerName;

    NetworkAddress serverAddr(ip, targetPort);
    auto tls=m_tlsOptions;if(tls.serverName.empty())tls.serverName=addressStr.substr(0,addressStr.find(':'));
    if (!(tls.enabled ? m_socket.OpenTls(tls,false,0,serverAddr) : m_socket.Open(0, true))) {
        m_disconnectReason="Transport initialization failed: "+m_socket.GetError();Log(m_disconnectReason);
        m_state = ConnectionState::Failed;
        return false;
    }

    m_serverConnection = NetworkConnection(serverAddr, 0);
    m_serverConnection.SetState(ConnectionState::Connecting);
    m_state = ConnectionState::Connecting;
    m_connectStartTime = m_currentTime;
    m_unacknowledgedInputs.clear();
    m_remotePlayers.clear();
    m_snapshotAssemblies.clear();
    m_hasWorldSnapshot = false;
    m_currentInputTick = 0;
    m_lastInputSentTime = -1.0;
    m_pendingInputTime = 0.0;
    m_pendingJump = false;
    m_pendingFire = false;
    m_disconnectReason.clear();

    Log("Connecting to server at " + m_serverAddressStr + "...");

    m_antiCheat.Initialize(this);

    // Send initial ConnectionRequest packet
    Packet req = Packet::CreateConnectionRequest(m_localPlayerName.substr(0, 64), 2);
    m_serverConnection.SendReliable(m_socket, req, m_currentTime);

    return true;
}

void NetworkClient::Disconnect(const std::string& reason) {
    m_antiCheat.Shutdown();
    if (m_state == ConnectionState::Connected || m_state == ConnectionState::Connecting) {
        m_disconnectReason = reason;
        Packet dc = Packet::CreateDisconnect(m_localClientId, reason);
        m_serverConnection.SendReliable(m_socket, dc, m_currentTime);
        m_state = ConnectionState::Disconnected;
        m_serverConnection.SetState(ConnectionState::Disconnected);
        m_remotePlayers.clear();
        m_unacknowledgedInputs.clear();
        Log("Disconnected from server: " + reason);

        if (m_onDisconnected) {
            m_onDisconnected(reason);
        }
    }
    m_socket.Close();m_state=ConnectionState::Disconnected;m_serverConnection.SetState(ConnectionState::Disconnected);
}

void NetworkClient::Log(const std::string& message) {
    if (m_onLog) {
        m_onLog(message);
    } else {
        std::cout << "[Client] " << message << std::endl;
    }
}

void NetworkClient::Update(double currentTime, double deltaTime) {
    if(!std::isfinite(currentTime)||!std::isfinite(deltaTime)||deltaTime<0||currentTime<m_currentTime)return;
    m_currentTime = currentTime;

    if (m_state == ConnectionState::Disconnected) return;

    // 1. Process incoming packets
    ProcessIncomingPackets(currentTime);

    if(m_tlsOptions.enabled&&(m_state==ConnectionState::Connecting||m_state==ConnectionState::Connected)&&!m_socket.GetError().empty()){
        m_disconnectReason=m_socket.GetError();m_state=ConnectionState::Failed;
        Log(m_disconnectReason);if(m_onDisconnected)m_onDisconnected(m_disconnectReason);m_socket.Close();return;
    }
    // 2. Handle connection handshake timeout
    if (m_state == ConnectionState::Connecting) {
        m_serverConnection.Update(m_socket,currentTime,deltaTime);
        if (currentTime - m_connectStartTime > (m_tlsOptions.enabled?10.0:4.0)) {
            Log("Connection failed: Server unavailable or timed out.");
            m_state = ConnectionState::Failed;
            m_disconnectReason = m_socket.GetError().empty()?"Server unavailable or timed out.":m_socket.GetError();
            if (m_onDisconnected) m_onDisconnected(m_disconnectReason);
            m_socket.Close();
            return;
        }
    }

    // 3. Handle active connection updates & timeouts
    if (m_state == ConnectionState::Connected) {
        m_serverConnection.Update(m_socket, currentTime, deltaTime);

        if (m_serverConnection.IsTimedOut(currentTime, CONNECTION_TIMEOUT_SECONDS)) {
            Log("Connection lost: Server timed out.");
            m_state = ConnectionState::Timeout;
            m_disconnectReason = "Connection lost: Server timed out.";
            if (m_onDisconnected) m_onDisconnected(m_disconnectReason);
            m_socket.Close();
            return;
        }

        // Periodic ping
        if (currentTime - m_lastPingSentTime >= 0.5) {
            m_lastPingSentTime = currentTime;
            SendPing(currentTime);
        }

        // 4. Update remote players snapshot interpolation
        for (auto& [netId, remote] : m_remotePlayers) {
            if (remote.networkObject) {
                remote.networkObject->Interpolate(currentTime, 0.08f);
                remote.latestState.position = remote.networkObject->GetPosition();
                remote.latestState.yaw = remote.networkObject->GetYaw();
                remote.latestState.pitch = remote.networkObject->GetPitch();
                remote.latestState.velocity = remote.networkObject->GetVelocity();
            }
        }
    }
}

void NetworkClient::ProcessIncomingPackets(double currentTime) {
    uint8_t buffer[MAX_PACKET_SIZE];
    NetworkAddress sender;
    size_t bytesRead = 0;

    for(unsigned count=0;count<1024&&m_socket.Receive(sender, buffer, sizeof(buffer), bytesRead);++count) {
        if (!(sender == m_serverConnection.GetAddress())) continue;
        if (bytesRead < sizeof(PacketHeader)) continue;

        PacketHeader header;
        BitReader reader;
        if (!Packet::Deserialize(buffer, bytesRead, header, reader)) {
            continue;
        }

        if (!m_serverConnection.ProcessIncomingHeader(header, currentTime)) continue;
        ProcessPacket(header, reader, currentTime);
    }
}

void NetworkClient::ProcessPacket(const PacketHeader& header, BitReader& reader, double currentTime) {
    PacketType type = static_cast<PacketType>(header.packetType);
    if (m_state != ConnectionState::Connected && type != PacketType::ConnectionAccepted &&
        type != PacketType::ConnectionRejected) return;

    switch (type) {
        case PacketType::ConnectionAccepted:
            HandleConnectionAccepted(reader, currentTime);
            break;
        case PacketType::ConnectionRejected:
            HandleConnectionRejected(reader, currentTime);
            break;
        case PacketType::Disconnect:
            HandleDisconnect(reader, currentTime);
            break;
        case PacketType::WorldSnapshot:
            HandleWorldSnapshot(reader, currentTime);
            break;
        case PacketType::WorldSnapshotChunk:
            HandleWorldSnapshotChunk(reader, currentTime);
            break;
        case PacketType::Pong:
            HandlePong(reader, currentTime);
            break;
        case PacketType::ChatMessage:
            HandleChatMessage(reader, currentTime);
            break;
        case PacketType::AntiCheatChallenge:
            HandleAntiCheatChallenge(reader, currentTime);
            break;
        default:
            break;
    }
}

void NetworkClient::HandleAntiCheatChallenge(BitReader& reader, double currentTime) {
    uint32_t nonce = 0;
    if (Packet::ReadAntiCheatChallenge(reader, nonce)) {
        Packet response = m_antiCheat.HandleServerChallenge(nonce);
        m_serverConnection.SendReliable(m_socket, response, currentTime);
    }
}

void NetworkClient::HandleConnectionAccepted(BitReader& reader, double currentTime) {
    if (m_state != ConnectionState::Connecting) return;
    uint32_t clientId = 0;
    uint32_t serverTick = 0;
    float tickRate = 60.0f;

    if (Packet::ReadConnectionAccepted(reader, clientId, serverTick, tickRate)) {
        m_localClientId = clientId;
        m_serverTick = serverTick;
        m_state = ConnectionState::Connected;
        m_serverConnection.SetState(ConnectionState::Connected);

        Log("========================================");
        Log("Connected to Server!");
        Log("  Assigned Client ID: " + std::to_string(clientId));
        Log("  Server Tick: " + std::to_string(serverTick));
        Log("  Server Tick Rate: " + std::to_string(static_cast<int>(tickRate)) + " Hz");
        Log("========================================");

        if (m_onConnected) {
            m_onConnected(clientId);
        }
    }
}

void NetworkClient::HandleConnectionRejected(BitReader& reader, double currentTime) {
    RejectReason reason = RejectReason::Unknown;
    std::string msg;
    Packet::ReadConnectionRejected(reader, reason, msg);

    m_state = ConnectionState::Failed;
    m_disconnectReason = "Connection Rejected: " + msg;
    Log(m_disconnectReason);

    if (m_onDisconnected) {
        m_onDisconnected(m_disconnectReason);
    }
    m_socket.Close();
}

void NetworkClient::HandleDisconnect(BitReader& reader, double currentTime) {
    uint32_t clientId = 0;
    std::string reason;
    Packet::ReadDisconnect(reader, clientId, reason);

    m_state = ConnectionState::Disconnected;
    m_disconnectReason = reason.empty() ? "Disconnected by server" : reason;
    Log("Server disconnected: " + m_disconnectReason);

    if (m_onDisconnected) {
        m_onDisconnected(m_disconnectReason);
    }
    m_socket.Close();
}

void NetworkClient::HandleWorldSnapshot(BitReader& reader, double currentTime) {
    WorldSnapshotData snapshot;
    if (!Packet::ReadWorldSnapshot(reader, snapshot)) return;
    if (m_hasWorldSnapshot && static_cast<int32_t>(snapshot.serverTick - m_serverTick) <= 0) return;
    m_hasWorldSnapshot = true;

    m_serverTick = snapshot.serverTick;

    // Track active network IDs to remove disconnected players
    std::unordered_set<uint32_t> activeRemoteNetIds;

    for (const auto& player : snapshot.players) {
        if (player.ownerClientId == m_localClientId) {
            // Local player state from server -> perform reconciliation
            m_localNetworkId = player.networkId;
            ReconcileServerState(player);
        } else {
            // Remote player -> push snapshot for interpolation
            activeRemoteNetIds.insert(player.networkId);

            auto it = m_remotePlayers.find(player.networkId);
            if (it == m_remotePlayers.end()) {
                // New remote player joined
                RemotePlayerInfo info;
                info.networkId = player.networkId;
                info.clientId = player.ownerClientId;
                info.playerName = player.playerName;
                info.latestState = player;
                info.pingMs = player.pingMs;
                info.networkObject = std::make_shared<NetworkObject>(player.networkId, player.ownerClientId);
                info.networkObject->SetPosition(player.position);
                info.networkObject->SetYaw(player.yaw);
                info.networkObject->SetPitch(player.pitch);
                info.networkObject->SetVelocity(player.velocity);

                TransformSnapshot snap;
                snap.timestamp = snapshot.timestamp;
                snap.position = player.position;
                snap.velocity = player.velocity;
                snap.yaw = player.yaw;
                snap.pitch = player.pitch;
                snap.flags = player.flags;
                info.networkObject->PushSnapshot(snap);

                m_remotePlayers[player.networkId] = info;
                Log("Remote player entered world: " + player.playerName + " (NetID: " + std::to_string(player.networkId) + ")");
            } else {
                // Existing remote player
                it->second.latestState = player;
                it->second.pingMs = player.pingMs;

                TransformSnapshot snap;
                snap.timestamp = snapshot.timestamp;
                snap.position = player.position;
                snap.velocity = player.velocity;
                snap.yaw = player.yaw;
                snap.pitch = player.pitch;
                snap.flags = player.flags;
                it->second.networkObject->PushSnapshot(snap);
            }
        }
    }

    // Remove remote players that left
    std::vector<uint32_t> toRemove;
    for (const auto& [netId, remote] : m_remotePlayers) {
        if (activeRemoteNetIds.find(netId) == activeRemoteNetIds.end()) {
            toRemove.push_back(netId);
        }
    }
    for (uint32_t netId : toRemove) {
        Log("Remote player left world: " + m_remotePlayers[netId].playerName);
        m_remotePlayers.erase(netId);
    }
}

void NetworkClient::HandleWorldSnapshotChunk(BitReader& reader, double currentTime) {
    uint32_t tick = 0, total = 0;
    uint16_t index = 0, count = 0;
    if (!reader.ReadUInt32(tick) || !reader.ReadUInt16(index) || !reader.ReadUInt16(count) ||
        !reader.ReadUInt32(total)) return;
    if (total < sizeof(PacketHeader) || total > 65535 + sizeof(PacketHeader) ||
        count == 0 || count > 55 || index >= count || count != (total + 1199) / 1200) return;
    if (m_hasWorldSnapshot && static_cast<int32_t>(tick - m_serverTick) <= 0) return;
    const size_t expected = index + 1 == count ? total - size_t(index) * 1200 : 1200;
    if (reader.GetRemainingBytes() != expected) return;
    for (auto it = m_snapshotAssemblies.begin(); it != m_snapshotAssemblies.end();) {
        if (currentTime - it->second.started > 1.0) it = m_snapshotAssemblies.erase(it);
        else ++it;
    }
    if (!m_snapshotAssemblies.count(tick) && m_snapshotAssemblies.size() >= 4) {
        auto oldest = std::min_element(m_snapshotAssemblies.begin(), m_snapshotAssemblies.end(),
            [](const auto& a, const auto& b) { return a.second.started < b.second.started; });
        m_snapshotAssemblies.erase(oldest);
    }
    auto& assembly = m_snapshotAssemblies[tick];
    if (assembly.parts.empty()) {
        assembly.started = currentTime;
        assembly.totalBytes = total;
        assembly.parts.resize(count);
    }
    if (assembly.totalBytes != total || assembly.parts.size() != count) return;
    if (!assembly.parts[index].empty()) return;
    assembly.parts[index].resize(expected);
    if (!reader.ReadBytes(assembly.parts[index].data(), expected)) return;
    std::vector<uint8_t> raw;
    for (const auto& part : assembly.parts) {
        if (part.empty()) return;
        raw.insert(raw.end(), part.begin(), part.end());
    }
    m_snapshotAssemblies.erase(tick);
    PacketHeader header;
    BitReader payload;
    if (!Packet::Deserialize(raw.data(), raw.size(), header, payload) ||
        header.packetType != static_cast<uint8_t>(PacketType::WorldSnapshot)) return;
    HandleWorldSnapshot(payload, currentTime);
}

void NetworkClient::HandlePong(BitReader& reader, double currentTime) {
    uint32_t pingId = 0;
    double clientTime = 0.0;
    double serverTime = 0.0;

    if (Packet::ReadPong(reader, pingId, clientTime, serverTime)) {
        double rtt = currentTime - clientTime;
        if (rtt >= 0.0 && rtt < 2.0) {
            float curPingMs = static_cast<float>(rtt * 1000.0);
            if (m_pingMs <= 0.001f) {
                m_pingMs = curPingMs;
            } else {
                m_pingMs = m_pingMs * 0.8f + curPingMs * 0.2f; // Smoothed exponential moving average
            }
        }
    }
}

void NetworkClient::HandleChatMessage(BitReader& reader, double currentTime) {
    uint32_t senderId = 0;
    std::string senderName, message;
    if (Packet::ReadChatMessage(reader, senderId, senderName, message)) {
        Log("[CHAT] " + senderName + ": " + message);
        if (m_onChatMessage) {
            m_onChatMessage(senderId, senderName, message);
        }
    }
}

void NetworkClient::SendPing(double currentTime) {
    Packet p = Packet::CreatePing(m_nextPingId++, currentTime);
    m_serverConnection.SendUnreliable(m_socket, p, currentTime);
}

void NetworkClient::SendChat(const std::string& message) {
    if (m_state != ConnectionState::Connected || message.empty()) return;
    Packet p = Packet::CreateChatMessage(m_localClientId, m_localPlayerName, message);
    m_serverConnection.SendReliable(m_socket, p, m_currentTime);
}

bool NetworkClient::CheckWorldCollision(const DirectX::XMFLOAT3& pos, float radius, float height) const {
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

void NetworkClient::PredictMovement(PlayerStateData& state, const PlayerInputData& input, float dt) {
    using namespace DirectX;

    state.yaw = input.lookYaw;
    state.pitch = input.lookPitch;

    float baseSpeed = 2.5f;
    if (input.crouch) baseSpeed *= 0.4f;
    else if (input.sprint) baseSpeed *= 2.2f;

    float sinY = std::sin(input.lookYaw);
    float cosY = std::cos(input.lookYaw);
    XMFLOAT3 forward(sinY, 0.0f, cosY);
    XMFLOAT3 right(cosY, 0.0f, -sinY);

    const float inputLength = std::sqrt(input.moveX * input.moveX + input.moveY * input.moveY);
    const float inputScale = inputLength > 1.0f ? 1.0f / inputLength : 1.0f;
    float desiredMoveX = (forward.x * input.moveY + right.x * input.moveX) * baseSpeed * inputScale;
    float desiredMoveZ = (forward.z * input.moveY + right.z * input.moveX) * baseSpeed * inputScale;

    state.velocity.x = desiredMoveX;
    state.velocity.z = desiredMoveZ;

    constexpr float gravity = -20.0f;
    state.velocity.y += gravity * dt;

    bool isGrounded = (state.flags & PlayerFlags::IsGrounded) != 0;
    if (input.jump && isGrounded) {
        state.velocity.y = 6.0f;
        isGrounded = false;
    }

    // Ground contact must be established again after every simulation step.
    isGrounded = false;

    XMFLOAT3 newPos = state.position;
    newPos.x += state.velocity.x * dt;
    newPos.z += state.velocity.z * dt;
    newPos.y += state.velocity.y * dt;

    if (newPos.y <= 1.0f) {
        newPos.y = 1.0f;
        state.velocity.y = 0.0f;
        isGrounded = true;
    }

    if (!CheckWorldCollision(newPos)) {
        state.position = newPos;
    } else {
        XMFLOAT3 tryX = state.position; tryX.x = newPos.x;
        if (!CheckWorldCollision(tryX)) state.position.x = newPos.x;

        XMFLOAT3 tryZ = state.position; tryZ.z = newPos.z;
        if (!CheckWorldCollision(tryZ)) state.position.z = newPos.z;

        XMFLOAT3 tryY = state.position; tryY.y = newPos.y;
        if (!CheckWorldCollision(tryY)) state.position.y = newPos.y;
        else {
            if (state.velocity.y <= 0.0f) isGrounded = true;
            state.velocity.y = 0.0f;
        }
    }

    uint8_t flags = PlayerFlags::IsAlive;
    if (isGrounded) flags |= PlayerFlags::IsGrounded;
    if (input.crouch) flags |= PlayerFlags::IsCrouching;
    if (input.sprint) flags |= PlayerFlags::IsSprinting;
    if (input.fire) flags |= PlayerFlags::IsFiring;
    state.flags = flags;
}

void NetworkClient::SendPlayerInput(const PlayerInputData& input, double currentTime) {
    if (m_state != ConnectionState::Connected || !std::isfinite(currentTime) ||
        !std::isfinite(input.deltaTime) || input.deltaTime <= 0.0f || input.deltaTime > .1f ||
        !std::isfinite(input.moveX) || !std::isfinite(input.moveY) ||
        !std::isfinite(input.lookYaw) || !std::isfinite(input.lookPitch)) return;

    PlayerInputData inputCopy = input;
    if (m_pendingInputTime == 0.0) ++m_currentInputTick;
    inputCopy.inputTick = m_currentInputTick;

    // 1. Apply local client prediction immediately
    PredictMovement(m_predictedLocalState, inputCopy, inputCopy.deltaTime);

    // 2. Buffer unacknowledged input for reconciliation
    m_unacknowledgedInputs.push_back(inputCopy);
    while (m_unacknowledgedInputs.size() > 1024) {
        m_unacknowledgedInputs.pop_front();
    }

    // Keep prediction at render frequency, but send at most 60 input packets/sec.
    // Every predicted subframe in this batch shares an acknowledgement tick.
    m_pendingInputTime += input.deltaTime;
    m_pendingJump = m_pendingJump || input.jump;
    m_pendingFire = m_pendingFire || input.fire;
    if (m_lastInputSentTime >= 0.0 && currentTime - m_lastInputSentTime < 1.0 / 60.0) return;
    inputCopy.deltaTime = static_cast<float>((std::min)(m_pendingInputTime, .1));
    inputCopy.jump = m_pendingJump;
    inputCopy.fire = m_pendingFire;
    Packet p = Packet::CreatePlayerInput(inputCopy);
    m_serverConnection.SendUnreliable(m_socket, p, currentTime);
    if (m_lastInputSentTime < 0.0 || currentTime - m_lastInputSentTime > 2.0 / 60.0)
        m_lastInputSentTime = currentTime;
    else m_lastInputSentTime += 1.0 / 60.0;
    m_pendingInputTime = 0.0;
    m_pendingJump = false;
    m_pendingFire = false;
}

void NetworkClient::ReconcileServerState(const PlayerStateData& serverState) {
    // 1. Remove all inputs that have been processed by the server up to serverState.lastProcessedInputTick
    while (!m_unacknowledgedInputs.empty() &&
           m_unacknowledgedInputs.front().inputTick <= serverState.lastProcessedInputTick) {
        m_unacknowledgedInputs.pop_front();
    }

    // 2. Check position deviation between predicted and server state
    float dx = m_predictedLocalState.position.x - serverState.position.x;
    float dy = m_predictedLocalState.position.y - serverState.position.y;
    float dz = m_predictedLocalState.position.z - serverState.position.z;
    float distSq = dx * dx + dy * dy + dz * dz;

    constexpr float errorThresholdSq = 0.05f * 0.05f; // 5 cm threshold
    if (distSq > errorThresholdSq) {
        // Replay all pending inputs on top of authoritative server state
        PlayerStateData replayed = serverState;
        for (const auto& input : m_unacknowledgedInputs) {
            PredictMovement(replayed, input, input.deltaTime);
        }
        m_predictedLocalState = replayed;
    }
}

} // namespace EngineNetwork
