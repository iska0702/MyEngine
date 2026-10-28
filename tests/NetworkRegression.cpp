#include "Networking/NetworkServer.h"
#include "Networking/NetworkClient.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <limits>
#include <cmath>

using namespace EngineNetwork;
static void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static void Pause() { std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
static void Send(NetworkSocket& socket, const NetworkAddress& address, Packet packet) {
    auto raw = packet.Serialize();
    Check(!raw.empty() && socket.Send(address, raw.data(), raw.size()), "UDP send failed");
}

static void TestServerInputs() {
    NetworkSocket probe;
    Check(probe.Open(0), "Port allocation failed");
    ServerConfig config;
    config.port = probe.GetBoundPort();
    probe.Close();
    NetworkServer server;
    server.SetOnLog([](const std::string&) {});
    Check(server.Start(config), "Server start failed");
    NetworkSocket socket;
    Check(socket.Open(0), "Client socket failed");
    NetworkAddress address("127.0.0.1", config.port);
    Send(socket, address, Packet::CreateConnectionRequest("Regression"));
    Pause(); server.Update(1.0, 0.016);
    Check(server.GetConnectedPlayerCount() == 1, "Handshake failed");
    const auto id = server.GetClients().begin()->first;
    const auto start = server.GetClients().at(id).playerState.position;
    PlayerInputData input;
    input.inputTick = 1; input.moveY = 1; input.deltaTime = 0.016f;
    auto packet = Packet::CreatePlayerInput(input); packet.header.sequence = 1;
    for (int i = 0; i < 4; ++i) Send(socket, address, packet);
    Pause(); server.Update(1.016, 0.016);
    auto position = server.GetClients().at(id).playerState.position;
    Check(std::abs(position.z - start.z - 0.04f) < 0.0001f, "Duplicate input was simulated twice");
    // Repeating inputTick with a new transport sequence must also be rejected.
    packet.header.sequence = 2; Send(socket, address, packet);
    Pause(); server.Update(1.032, 0.016);
    Check(std::abs(server.GetClients().at(id).playerState.position.z - position.z - .04f) < .0001f,
          "Replayed input changed server simulation speed");
    input.inputTick = 2; input.lookYaw = std::numeric_limits<float>::quiet_NaN();
    packet = Packet::CreatePlayerInput(input); packet.header.sequence = 3;
    Send(socket, address, packet); Pause(); server.Update(1.048, 0.016);
    auto clean = server.GetClients().at(id).playerState.position;
    Check(std::isfinite(clean.x) && std::isfinite(clean.z), "NaN contaminated server state");
    input.lookYaw = 0; input.deltaTime = 0.1f;
    for (int i = 0; i < 10; ++i) {
        input.inputTick = 10 + i;
        packet = Packet::CreatePlayerInput(input); packet.header.sequence = 10 + i;
        Send(socket, address, packet);
    }
    Pause(); server.Update(1.064, 0.016);
    Check(server.GetClients().at(id).playerState.position.z - clean.z <= 0.0401f, "Input flood exceeded tick budget");
    // A connection request retransmission must not replace an existing player.
    Send(socket, address, Packet::CreateConnectionRequest("Regression"));
    Pause(); server.Update(1.08, 0.016);
    Check(server.GetClients().count(id) == 1, "Handshake retry replaced client");
    input.inputTick = 50; input.moveX = 1; input.moveY = 1; input.deltaTime = .016f;
    const auto beforeDiagonal = server.GetClients().at(id).playerState.position;
    packet = Packet::CreatePlayerInput(input); packet.header.sequence = 50;
    Send(socket, address, packet); Pause(); server.Update(1.1, .016);
    const auto afterDiagonal = server.GetClients().at(id).playerState.position;
    const float diagonalX = afterDiagonal.x - beforeDiagonal.x, diagonalZ = afterDiagonal.z - beforeDiagonal.z;
    Check(std::abs(std::sqrt(diagonalX*diagonalX + diagonalZ*diagonalZ) - .04f) < .0001f,
          "Diagonal input bypassed server movement speed");
    input.moveX = 0;
    // Holding the trigger is normal: the server suppresses shots within the cooldown.
    input.moveY = 0; input.fire = true; input.deltaTime = 0.01f;
    for (int i = 0; i < 20; ++i) {
        input.inputTick = 100 + i;
        packet = Packet::CreatePlayerInput(input); packet.header.sequence = 100 + i;
        Send(socket, address, packet); Pause(); server.Update(2.0 + i * 0.01, 0.01);
        Check(server.GetConnectedPlayerCount() == 1, "Held fire falsely kicked a player");
    }
    // Count repeated datagrams too, before transport deduplication.
    for (int i = 0; i < 140; ++i) Send(socket, address, packet);
    Pause(); server.Update(2.3, 0.01);
    Check(server.GetConnectedPlayerCount() == 0, "Duplicate packet flood bypassed limiter/deferred kick");
    server.Stop();
}

static void TestClientSnapshotsAndSender() {
    NetworkSocket trusted, attacker;
    Check(trusted.Open(0) && attacker.Open(0), "Snapshot socket setup failed");
    NetworkClient client;
    client.SetOnLog([](const std::string&) {});
    Check(client.Connect("127.0.0.1", trusted.GetBoundPort(), "Tester"), "Client connect failed");
    Pause();
    uint8_t buffer[MAX_PACKET_SIZE]; size_t size = 0; NetworkAddress clientAddress;
    Check(trusted.Receive(clientAddress, buffer, sizeof(buffer), size), "Connection request not received");
    auto accept = Packet::CreateConnectionAccepted(999, 0, 60);
    accept.header.sequence = 1;
    Send(attacker, clientAddress, accept); Pause(); client.Update(0.02, 0.02);
    Check(client.IsConnecting(), "Foreign sender changed connection state");
    Send(trusted, clientAddress, accept); Pause(); client.Update(0.04, 0.02);
    Check(client.IsConnected(), "Trusted connection response rejected");
    WorldSnapshotData snapshot; snapshot.serverTick = 1;
    for (unsigned i = 0; i < 64; ++i) {
        PlayerStateData player;
        player.networkId = i + 1; player.ownerClientId = i + 1;
        player.playerName = "Player" + std::to_string(i);
        snapshot.players.push_back(player);
    }
    auto chunks = Packet::CreateWorldSnapshotChunks(snapshot);
    Check(chunks.size() > 1, "Large snapshot was not split");
    uint16_t sequence = 2;
    // Deliver a complete snapshot backwards; no partial world is published.
    for (auto it = chunks.rbegin(); it != chunks.rend(); ++it) {
        Check(it->Serialize().size() <= MAX_PACKET_SIZE, "Snapshot datagram exceeds MTU budget");
        it->header.sequence = sequence++;
        Send(trusted, clientAddress, *it);
        Pause(); client.Update(0.04 + sequence * 0.01, 0.01);
        if (it + 1 != chunks.rend()) Check(client.GetRemotePlayers().empty(), "Partial snapshot published");
    }
    Check(client.GetRemotePlayers().size() == 64, "Chunk reassembly lost players");
    auto disconnect = Packet::CreateDisconnect(999, "spoofed"); disconnect.header.sequence = sequence++;
    Send(attacker, clientAddress, disconnect); Pause(); client.Update(0.5, 0.02);
    Check(client.IsConnected(), "Foreign disconnect accepted");
    // Lost/incomplete snapshots must not prune the previous complete world.
    snapshot.serverTick = 2; snapshot.players.resize(32);
    chunks = Packet::CreateWorldSnapshotChunks(snapshot);
    chunks[0].header.sequence = sequence++;
    Send(trusted, clientAddress, chunks[0]); Pause(); client.Update(0.6, 0.02);
    Check(client.GetRemotePlayers().size() == 64, "Incomplete snapshot changed world");
    snapshot.serverTick = 3; snapshot.players.resize(24);
    for (auto& chunk : Packet::CreateWorldSnapshotChunks(snapshot)) {
        chunk.header.sequence = sequence++; Send(trusted, clientAddress, chunk);
    }
    Pause(); client.Update(0.7, 0.02);
    Check(client.GetRemotePlayers().size() == 24, "New complete snapshot was not applied");
    // High render FPS must not trigger the server's 120 datagram/sec limit.
    PlayerInputData fastInput; fastInput.deltaTime = .001f; fastInput.moveX = 1; fastInput.moveY = 1;
    const auto predictionStart = client.GetPredictedLocalPlayerState().position;
    for (int i = 0; i < 1000; ++i) client.SendPlayerInput(fastInput, 1.0 + i * .001);
    Pause();
    unsigned inputs = 0; double simulatedTime = 0;
    while (trusted.Receive(clientAddress, buffer, sizeof(buffer), size)) {
        PacketHeader h; BitReader r;
        if (Packet::Deserialize(buffer, size, h, r) && h.packetType == static_cast<uint8_t>(PacketType::PlayerInput)) {
            PlayerInputData decoded; Check(Packet::ReadPlayerInput(r, decoded), "Invalid batched input");
            ++inputs; simulatedTime += decoded.deltaTime;
        }
    }
    Check(inputs >= 40 && inputs <= 61, "Input packet rate follows render FPS");
    Check(simulatedTime > .9 && simulatedTime <= 1.001, "Batched inputs lost elapsed movement time");
    const auto predicted = client.GetPredictedLocalPlayerState().position;
    const float dx = predicted.x - predictionStart.x, dz = predicted.z - predictionStart.z;
    Check(std::abs(std::sqrt(dx*dx+dz*dz) - 2.5f) < .01f, "Diagonal prediction exceeds normal movement speed");
    client.Disconnect();
}

static void TestMovementAcrossFrameRates() {
    for (const int fps : {30, 75, 240}) {
        NetworkSocket probe; Check(probe.Open(0), "Frame rate port failed");
        ServerConfig config; config.port = probe.GetBoundPort(); probe.Close();
        NetworkServer server; server.SetOnLog([](const std::string&) {});
        Check(server.Start(config), "Frame rate server failed");
        NetworkClient client; client.SetOnLog([](const std::string&) {});
        Check(client.Connect("127.0.0.1", config.port, "FrameRate"), "Frame rate connect failed");
        Pause(); server.Update(.01, .016); Pause(); client.Update(.02, .016);
        Check(client.IsConnected(), "Frame rate handshake failed");
        const auto id = client.GetLocalClientId();
        const auto start = server.GetClients().at(id).playerState.position;
        PlayerInputData input; input.moveY = 1; input.deltaTime = 1.0f / fps;
        double nextInput = .02;
        for (int tick = 1; tick <= 60; ++tick) {
            const double time = .02 + tick / 60.0;
            while (nextInput <= time + 1e-8) {
                client.SendPlayerInput(input, nextInput); nextInput += 1.0 / fps;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            server.Update(time, 1.0 / 60.0); client.Update(time, 1.0 / 60.0);
            Check(server.GetConnectedPlayerCount() == 1, "Render FPS triggered a false anti-cheat kick");
        }
        auto position = server.GetClients().at(id).playerState.position;
        Check(std::abs(position.z - start.z - 2.5f) < .05f, "Render FPS changed authoritative movement speed");
        // Transport heartbeats alone must not keep movement held indefinitely.
        for (int tick = 1; tick <= 30; ++tick) {
            const double time = 1.02 + tick / 60.0;
            server.Update(time, 1.0 / 60.0); client.Update(time, 1.0 / 60.0);
        }
        position = server.GetClients().at(id).playerState.position;
        server.Update(1.55, 1.0 / 60.0);
        Check(server.GetClients().at(id).playerState.position.z == position.z, "Stale input kept moving after timeout");
        client.Disconnect(); server.Stop();
    }
}

static void TestAntiCheatRules() {
    ServerAntiCheat anti;
    anti.Initialize(nullptr);
    PlayerInputData input; input.fire = true;
    DirectX::XMFLOAT3 position{0, 1, 0}, velocity{};
    uint8_t flags = PlayerFlags::IsGrounded;
    Check(!anti.ValidateCombatAction(99, input, 0), "Unregistered shooter accepted");
    Check(!anti.ValidateMovement(99, input, .016f, position, velocity, flags), "Unregistered movement accepted");
    anti.RegisterClient(1, "Test", "127.0.0.1", position);
    Check(anti.ValidateCombatAction(1, input, 0), "First shot at time zero rejected");
    Check(!anti.ValidateCombatAction(1, input, .01), "Cooldown bypass at time zero");
    Check(anti.ValidateCombatAction(1, input, .1), "Shot at cooldown boundary rejected");
    Check(!anti.ValidateCombatAction(1, input, std::numeric_limits<double>::quiet_NaN()), "NaN fire time accepted");
    Check(anti.GetRecentViolations().empty(), "Ordinary cooldown generated strikes");
    position.x = std::numeric_limits<float>::quiet_NaN();
    Check(!anti.ValidateMovement(1, input, .016f, position, velocity, flags), "NaN position accepted");
    Check(std::isfinite(position.x), "NaN position not corrected");
    position = {0, 1, 0};
    Check(!anti.ValidateMovement(1, input, std::numeric_limits<float>::infinity(), position, velocity, flags), "Infinite dt accepted");
    // Grounded on an elevated platform must not look like flying.
    anti.RegisterClient(2, "Platform", "127.0.0.1", {0, 8, 0});
    position = {0, 8, 0}; velocity = {};
    for (int i = 0; i < 300; ++i)
        Check(anti.ValidateMovement(2, input, .016f, position, velocity, flags), "Standing on a platform flagged as flyhack");
    position.x = 100;
    Check(!anti.ValidateMovement(2, input, .016f, position, velocity, flags), "Teleport accepted");
    Check(position.x == 0, "Teleport not rubberbanded");
    anti.RegisterClient(3, "Rate", "127.0.0.1", {0, 1, 0});
    for (int i = 0; i < 120; ++i) Check(anti.ValidatePacketRate(3, 5), "Rate limit fired early");
    Check(!anti.ValidatePacketRate(3, 5), "Flood accepted");
    const auto violations = anti.GetRecentViolations().size();
    for (int i = 0; i < 200; ++i) Check(!anti.ValidatePacketRate(3, 5), "Flood resumed within window");
    Check(anti.GetRecentViolations().size() == violations, "Flood amplified violation logs");
    Check(anti.ValidatePacketRate(3, 6), "Packet window did not reset");
}

static void TestIntegrityDeadlines() {
    NetworkSocket probe;
    Check(probe.Open(0), "Integrity test port allocation failed");
    ServerConfig config; config.port = probe.GetBoundPort(); probe.Close();
    NetworkServer server; server.SetOnLog([](const std::string&) {});
    Check(server.Start(config), "Integrity server failed");
    NetworkSocket peer; Check(peer.Open(0), "Integrity peer failed");
    NetworkAddress address("127.0.0.1", config.port);
    uint16_t sequence = 1;
    uint16_t latestChallengeSequence = 0; bool hasChallenge = false;
    auto join = [&] {
        hasChallenge = false;
        auto request = Packet::CreateConnectionRequest("Integrity"); request.header.sequence = sequence++;
        Send(peer, address, request); Pause(); server.Update(server.GetUptimeSeconds() + .01, .01);
        Check(server.GetConnectedPlayerCount() == 1, "Reconnection blocked by an automatic IP ban");
    };
    auto challenge = [&] {
        uint32_t nonce = 0; uint8_t buffer[MAX_PACKET_SIZE]; size_t size; NetworkAddress from;
        Pause();
        while (peer.Receive(from, buffer, sizeof(buffer), size)) {
            PacketHeader header; BitReader reader;
            if (Packet::Deserialize(buffer, size, header, reader) &&
                header.packetType == static_cast<uint8_t>(PacketType::AntiCheatChallenge) &&
                (!hasChallenge || static_cast<int16_t>(header.sequence - latestChallengeSequence) > 0)) {
                Check(Packet::ReadAntiCheatChallenge(reader, nonce), "Malformed server challenge");
                latestChallengeSequence = header.sequence; hasChallenge = true;
            }
        }
        Check(nonce != 0, "Server challenge missing"); return nonce;
    };
    auto respond = [&](uint32_t nonce, uint64_t hash, uint32_t flags, double time) {
        auto packet = Packet::CreateAntiCheatResponse(nonce, hash ^ (uint64_t(nonce) * 1099511628211ULL), flags);
        packet.header.sequence = sequence++; Send(peer, address, packet); Pause(); server.Update(time, .016);
    };
    join(); auto heartbeat = [&] (double time) { auto p = Packet::CreateHeartbeat(); p.header.sequence = sequence++; Send(peer,address,p); Pause(); server.Update(time,.016); };
    heartbeat(10); auto nonce = challenge();
    respond(nonce, 1234, 0, 10.1);
    Check(server.GetConnectedPlayerCount() == 1, "Valid integrity response rejected");
    respond(nonce, 1234, 0, 10.2);
    Check(server.GetConnectedPlayerCount() == 1, "Duplicate integrity reply falsely kicked client");
    heartbeat(20); nonce = challenge();
    respond(nonce, 5678, 0, 20.1);
    Check(server.GetConnectedPlayerCount() == 0, "Session code hash change ignored");
    join(); heartbeat(30); nonce = challenge();
    respond(nonce, 0, 0, 30.1);
    Check(server.GetConnectedPlayerCount() == 0, "Nonce encoding hid a zero executable hash");
    join(); heartbeat(40); nonce = challenge();
    respond(nonce, 1234, 1, 40.1);
    Check(server.GetConnectedPlayerCount() == 0, "Reported integrity failure ignored");
    join(); heartbeat(50); nonce = challenge();
    respond(nonce, 1234, 0, 58.1);
    Check(server.GetConnectedPlayerCount() == 0, "Late response bypassed deadline");
    join(); heartbeat(60); challenge();
    // A long update gap previously overwrote the unanswered nonce before checking timeout.
    server.Update(80, .016);
    Check(server.GetConnectedPlayerCount() == 0, "Long frame reset expired integrity challenge");
    Check(server.GetAntiCheat().GetRecentViolations().back().timestamp == 80, "Violation timestamp missing");
    server.Stop();
}

static void TestReplayAndQueueBounds(){
    NetworkSocket receiver,sender;Check(receiver.Open(0)&&sender.Open(0),"Queue test sockets failed");
    NetworkConnection connection(NetworkAddress("127.0.0.1",receiver.GetBoundPort()),1);connection.SetState(ConnectionState::Connected);
    PacketHeader header;header.sequence=10;header.ack=500;Check(connection.ProcessIncomingHeader(header,1),"Initial sequence failed");
    for(int i=0;i<256;++i){auto packet=Packet::CreateHeartbeat();Check(connection.SendReliable(sender,packet,1.1),"Reliable queue filled prematurely");}
    header.ack=255;Check(!connection.ProcessIncomingHeader(header,2),"Replay accepted");
    Check(connection.GetLastReceivedTime()==1,"Replay refreshed liveness");
    auto packet=Packet::CreateHeartbeat();Check(!connection.SendReliable(sender,packet,2),"Replay acknowledged pending data or queue is unbounded");
    for(int i=0;i<12;++i)connection.Update(sender,2+i*.2,.2);
    Check(connection.SendReliable(sender,packet,5),"Expired reliable packets were never released");
}
static void TestJoinBudget(){
    NetworkServer server;server.SetOnLog([](const std::string&){});ServerConfig config;config.port=0;config.maxPlayers=1;Check(server.Start(config),"Join budget server failed");
    std::vector<std::unique_ptr<NetworkSocket>> peers;
    NetworkAddress address("127.0.0.1",server.GetConfig().port);
    for(int i=0;i<70;++i){auto peer=std::make_unique<NetworkSocket>();Check(peer->Open(0),"Join socket failed");Send(*peer,address,Packet::CreateConnectionRequest("Budget"));peers.push_back(std::move(peer));}
    Pause();server.Update(1,.016);Check(server.GetConnectedPlayerCount()==1,"Join budget setup failed");
    server.KickPlayer(server.GetClients().begin()->first,"test");
    Send(*peers.back(),address,Packet::CreateConnectionRequest("Budget"));Pause();server.Update(1.1,.016);
    Check(server.GetConnectedPlayerCount()==0,"IP join budget bypassed");
    Send(*peers.back(),address,Packet::CreateConnectionRequest("Budget"));Pause();server.Update(12,.016);
    Check(server.GetConnectedPlayerCount()==1,"Expired IP budget did not recover");
}
static void TestChatIdentityAndRate(){
    NetworkServer server;std::string logs;unsigned received=0;
    server.SetOnLog([&](const std::string& text){logs+=text+"\n";});server.SetOnChatMessage([&](uint32_t,const std::string& name,const std::string&){Check(name=="Trusted","Chat identity forged");++received;});
    ServerConfig config;config.port=0;Check(server.Start(config),"Chat server failed");NetworkSocket peer;Check(peer.Open(0),"Chat peer failed");NetworkAddress address("127.0.0.1",server.GetConfig().port);
    Send(peer,address,Packet::CreateConnectionRequest("Trusted"));Pause();server.Update(1,.016);
    for(int i=1;i<=8;++i){auto packet=Packet::CreateChatMessage(999,"ADMIN\n\x1b[2J","spoof-probe");packet.header.sequence=uint16_t(i);Send(peer,address,packet);}
    Pause();server.Update(1.1,.016);
    Check(received==1&&logs.find("ADMIN")==std::string::npos,"Chat flood or forged log identity accepted");
    auto packet=Packet::CreateChatMessage(0,"wrong","normal");packet.header.sequence=9;Send(peer,address,packet);Pause();server.Update(1.4,.016);
    Check(received==2,"Chat rate limiter did not recover");
}
int main() {
    try {
        TestAntiCheatRules();
        TestIntegrityDeadlines();
        TestReplayAndQueueBounds();TestJoinBudget();TestChatIdentityAndRate();
        TestServerInputs();
        TestClientSnapshotsAndSender();
        TestMovementAcrossFrameRates();
        NetworkConnection connection;
        PacketHeader header; header.sequence = 65535;
        Check(connection.ProcessIncomingHeader(header, 1), "Initial header rejected");
        Check(!connection.ProcessIncomingHeader(header, 2), "Duplicate header accepted");
        header.sequence = 0;
        Check(connection.ProcessIncomingHeader(header, 3), "Sequence wrap failed");
        header.sequence = 32;
        Check(connection.ProcessIncomingHeader(header, 4), "32-sequence gap failed");
        std::cout << "PASS: replay ACK/liveness rejection, bounded reliable queue, join budget, chat identity/rate, held-fire cooldown, finite movement, platform grounding, integrity replay/deadlines/hash continuity, duplicate flood, replay, tick budget, handshake retry, deferred kick, sender filtering, snapshots, sequence wrap, movement at 30/75/240 FPS and stale-input timeout\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
