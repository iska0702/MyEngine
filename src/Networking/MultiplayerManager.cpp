#include "MultiplayerManager.h"
#include "Renderer/Renderer.h"
#include "Renderer/Camera.h"
#include "Core/Window.h"
#include "Input/DualSense.h"
#ifndef ENGINE_RELEASE
#include <imgui.h>
#endif
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace EngineNetwork {

MultiplayerManager& MultiplayerManager::Get() {
    static MultiplayerManager s_instance;
    return s_instance;
}

MultiplayerManager::MultiplayerManager() {TlsOptions options;options.enabled=true;m_client.SetTlsOptions(options);}

MultiplayerManager::~MultiplayerManager() {
    Shutdown();
}

void MultiplayerManager::Initialize() {
    m_client.SetOnConnected([this](uint32_t clientId) {
        m_chatHistory.push_back("[SYSTEM] Successfully connected to server (Assigned ID #" + std::to_string(clientId) + ")");
    });

    m_client.SetOnDisconnected([this](const std::string& reason) {
        m_chatHistory.push_back("[SYSTEM] Disconnected: " + reason);
    });

    m_client.SetOnChatMessage([this](uint32_t senderId, const std::string& name, const std::string& message) {
        m_chatHistory.push_back(name + ": " + message);
        if (m_chatHistory.size() > 50) m_chatHistory.erase(m_chatHistory.begin());
    });
}

void MultiplayerManager::Shutdown() {
    Disconnect();
}

bool MultiplayerManager::ConnectToServer(const std::string& address, uint16_t port, const std::string& playerName) {
    return m_client.Connect(address, port, playerName);
}

void MultiplayerManager::Disconnect() {
    m_client.Disconnect("User requested disconnect");
}

bool MultiplayerManager::IsMultiplayerActive() const {
    return m_client.GetState() == ConnectionState::Connected || m_client.GetState() == ConnectionState::Connecting;
}

bool MultiplayerManager::IsConnected() const {
    return m_client.IsConnected();
}

bool MultiplayerManager::IsConnecting() const {
    return m_client.IsConnecting();
}

std::string MultiplayerManager::GetConnectionStateString() const {
    switch (m_client.GetState()) {
        case ConnectionState::Disconnected: return "Disconnected";
        case ConnectionState::Connecting: return "Connecting...";
        case ConnectionState::Connected: return "Connected";
        case ConnectionState::Disconnecting: return "Disconnecting...";
        case ConnectionState::Failed: return "Connection Failed";
        case ConnectionState::Timeout: return "Connection Lost (Timeout)";
        default: return "Unknown";
    }
}

size_t MultiplayerManager::GetConnectedPlayerCount() const {
    if (!m_client.IsConnected()) return 0;
    return 1 + m_client.GetRemotePlayers().size(); // Local player + remote players
}

void MultiplayerManager::SendChatMessage(const std::string& message) {
    m_client.SendChat(message);
}

// Generate simple 3D Capsule / Avatar Mesh for remote player visualization
static void GeneratePlayerMesh(std::vector<Renderer::Vertex>& outVerts, std::vector<uint32_t>& outIndices, const DirectX::XMFLOAT4& color) {
    outVerts.clear();
    outIndices.clear();

    const int segments = 12;
    const int rings = 8;
    const float radius = 0.35f;
    const float height = 1.4f;

    // Body Cylinder
    for (int y = 0; y <= 1; ++y) {
        float py = (y == 0) ? -height * 0.5f : height * 0.5f;
        for (int i = 0; i <= segments; ++i) {
            float u = (float)i / segments;
            float angle = u * DirectX::XM_2PI;
            float px = std::cos(angle) * radius;
            float pz = std::sin(angle) * radius;

            Renderer::Vertex v;
            v.position = { px, py + 0.9f, pz };
            v.normal = { px / radius, 0.0f, pz / radius };
            v.color = color;
            outVerts.push_back(v);
        }
    }

    int stride = segments + 1;
    for (int i = 0; i < segments; ++i) {
        int i0 = i;
        int i1 = i + 1;
        int i2 = i + stride;
        int i3 = i + stride + 1;

        outIndices.push_back(i0);
        outIndices.push_back(i2);
        outIndices.push_back(i1);

        outIndices.push_back(i1);
        outIndices.push_back(i2);
        outIndices.push_back(i3);
    }

    // Head Sphere at top
    size_t headBaseIdx = outVerts.size();
    float headY = height * 0.5f + 0.9f + 0.2f;
    float headRadius = 0.25f;

    for (int r = 0; r <= rings; ++r) {
        float phi = (float)r / rings * DirectX::XM_PI;
        for (int s = 0; s <= segments; ++s) {
            float theta = (float)s / segments * DirectX::XM_2PI;
            float px = headRadius * std::sin(phi) * std::cos(theta);
            float py = headRadius * std::cos(phi);
            float pz = headRadius * std::sin(phi) * std::sin(theta);

            Renderer::Vertex v;
            v.position = { px, py + headY, pz };
            v.normal = { px / headRadius, py / headRadius, pz / headRadius };
            v.color = color;
            outVerts.push_back(v);
        }
    }

    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            int i0 = static_cast<int>(headBaseIdx + r * stride + s);
            int i1 = i0 + 1;
            int i2 = i0 + stride;
            int i3 = i2 + 1;

            outIndices.push_back(i0);
            outIndices.push_back(i2);
            outIndices.push_back(i1);

            outIndices.push_back(i1);
            outIndices.push_back(i2);
            outIndices.push_back(i3);
        }
    }
}

// Color palette for distinct remote players
static DirectX::XMFLOAT3 GetPlayerPaletteColor(uint32_t clientId) {
    static const DirectX::XMFLOAT3 colors[] = {
        { 0.1f, 0.8f, 1.0f }, // Cyan
        { 1.0f, 0.4f, 0.1f }, // Orange
        { 0.2f, 1.0f, 0.3f }, // Lime Green
        { 0.9f, 0.2f, 0.8f }, // Magenta
        { 1.0f, 0.9f, 0.2f }, // Yellow/Gold
        { 0.4f, 0.5f, 1.0f }  // Blue
    };
    return colors[clientId % 6];
}

void MultiplayerManager::SyncRemotePlayerVisuals(Renderer* renderer) {
    if (!renderer) return;

    const auto& remotePlayers = m_client.GetRemotePlayers();

    // 1. Create 3D avatars for newly connected remote players
    for (const auto& [netId, player] : remotePlayers) {
        if (m_remoteModelIds.find(netId) == m_remoteModelIds.end()) {
            std::vector<Renderer::Vertex> verts;
            std::vector<uint32_t> idx;
            DirectX::XMFLOAT3 col = GetPlayerPaletteColor(player.clientId);
            GeneratePlayerMesh(verts, idx, DirectX::XMFLOAT4(col.x, col.y, col.z, 1.0f));

            Renderer::PBRMaterial mat;
            mat.albedo = col;
            mat.roughness = 0.3f;
            mat.metallic = 0.1f;
            mat.reflectance = 0.5f;

            std::string name = "RemotePlayer_" + std::to_string(player.clientId) + "_" + player.playerName;
            uint32_t modelId = renderer->CreateDynamicClothModel(name, verts, idx, mat);
            if (modelId != 0) {
                m_remoteModelIds[netId] = modelId;
            }
        }
    }

    // 2. Remove visuals for disconnected remote players
    std::vector<uint32_t> toDelete;
    for (const auto& [netId, modelId] : m_remoteModelIds) {
        if (remotePlayers.find(netId) == remotePlayers.end()) {
            renderer->RemoveDynamicModel(modelId);
            toDelete.push_back(netId);
        }
    }
    for (uint32_t netId : toDelete) {
        m_remoteModelIds.erase(netId);
    }

    // 3. Update transforms and matrices for active remote players
    auto& dynModels = renderer->GetDynamicModels();
    for (const auto& [netId, player] : remotePlayers) {
        auto it = m_remoteModelIds.find(netId);
        if (it == m_remoteModelIds.end()) continue;

        uint32_t modelId = it->second;
        for (auto& dm : dynModels) {
            if (dm.modelId == modelId) {
                dm.position = player.latestState.position;
                dm.rotation = DirectX::XMFLOAT3(0.0f, player.latestState.yaw, 0.0f);
                dm.visible = true;

                using namespace DirectX;
                XMMATRIX S = XMMatrixScaling(1.0f, 1.0f, 1.0f);
                XMMATRIX R = XMMatrixRotationRollPitchYaw(0.0f, dm.rotation.y, 0.0f);
                XMMATRIX T = XMMatrixTranslation(dm.position.x, dm.position.y, dm.position.z);
                XMStoreFloat4x4(&dm.worldMatrix, S * R * T);
                break;
            }
        }
    }
}

void MultiplayerManager::CleanupRemotePlayerVisuals(Renderer* renderer) {
    if (!renderer) return;
    for (const auto& [netId, modelId] : m_remoteModelIds) {
        renderer->RemoveDynamicModel(modelId);
    }
    m_remoteModelIds.clear();
}

void MultiplayerManager::Update(float dt, Renderer* renderer, Camera* camera, Window* window) {
    m_elapsedTime += dt;

    // Collect scene obstacles for client-side prediction
    if (renderer) {
        std::vector<NetworkClient::WorldCollisionBox> colBoxes;
        for (const auto& m : renderer->GetModels()) {
            if (m.name != "Floor" && m.visible && m.hasCollision) {
                NetworkClient::WorldCollisionBox b;
                float hx = (std::max)(m.scale.x * 0.5f, 0.1f);
                float hy = (std::max)(m.scale.y * 0.5f, 0.1f);
                float hz = (std::max)(m.scale.z * 0.5f, 0.1f);
                b.minPoint = DirectX::XMFLOAT3(m.position.x - hx, m.position.y - hy, m.position.z - hz);
                b.maxPoint = DirectX::XMFLOAT3(m.position.x + hx, m.position.y + hy, m.position.z + hz);
                colBoxes.push_back(b);
            }
        }
        m_client.SetWorldCollisionBoxes(colBoxes);
    }

    // Update networking client
    m_client.Update(m_elapsedTime, dt);

    if (m_client.IsConnected()) {
        if (!m_wasConnected) {
            m_wasConnected = true;
            // Initialize local prediction position to camera position
            if (camera) {
                m_client.SetPredictedPosition(camera->GetPosition());
            }
        }

        // Sample user input and send to server with prediction
        if (camera && window) {
            PlayerInputData input;
            input.moveX = 0.0f;
            input.moveY = 0.0f;

            if (!window->IsCursorEnabled() || camera->IsFreecamMode()) {
                if (window->IsKeyDown('W')) input.moveY += 1.0f;
                if (window->IsKeyDown('S')) input.moveY -= 1.0f;
                if (window->IsKeyDown('A')) input.moveX -= 1.0f;
                if (window->IsKeyDown('D')) input.moveX += 1.0f;

                input.jump = window->IsKeyDown(VK_SPACE);
                input.crouch = window->IsKeyDown(VK_CONTROL);
                input.sprint = window->IsKeyDown(VK_SHIFT);
                input.fire = window->IsKeyDown('G');

                auto& dualSense = EngineInput::DualSenseController::Get();
                if (dualSense.IsConnected()) {
                    float padLX = dualSense.GetLeftStickX();
                    float padLY = dualSense.GetLeftStickY();
                    if (std::abs(padLX) > 0.05f) input.moveX += padLX;
                    if (std::abs(padLY) > 0.05f) input.moveY += padLY;

                    if (dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadUp)) input.moveY += 1.0f;
                    if (dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadDown)) input.moveY -= 1.0f;
                    if (dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadLeft)) input.moveX -= 1.0f;
                    if (dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadRight)) input.moveX += 1.0f;

                    if (dualSense.IsButtonDown(EngineInput::DualSenseButton::Cross)) input.jump = true;
                    if (dualSense.IsButtonDown(EngineInput::DualSenseButton::Circle) || dualSense.IsButtonDown(EngineInput::DualSenseButton::R3)) input.crouch = true;
                    if (dualSense.IsButtonDown(EngineInput::DualSenseButton::L3)) input.sprint = true;
                    if (dualSense.GetRightTrigger() > 0.3f || dualSense.IsButtonDown(EngineInput::DualSenseButton::R1) || dualSense.IsButtonDown(EngineInput::DualSenseButton::Square)) input.fire = true;
                }

                input.moveX = (std::clamp)(input.moveX, -1.0f, 1.0f);
                input.moveY = (std::clamp)(input.moveY, -1.0f, 1.0f);
            }

            DirectX::XMFLOAT3 fwd = camera->GetForwardVector();
            input.lookYaw = std::atan2(fwd.x, fwd.z);
            input.lookPitch = std::asin((std::clamp)(fwd.y, -0.99f, 0.99f));
            input.deltaTime = dt;

            m_client.SendPlayerInput(input, m_elapsedTime);

            // Sync camera position with predicted local player position (if not in freecam)
            if (!camera->IsFreecamMode()) {
                const auto& pred = m_client.GetPredictedLocalPlayerState();
                camera->SetPosition(pred.position.x, pred.position.y + 0.6f, pred.position.z);
            }
        }

        // Synchronize remote player 3D meshes in Renderer
        SyncRemotePlayerVisuals(renderer);
    } else {
        if (m_wasConnected) {
            m_wasConnected = false;
            CleanupRemotePlayerVisuals(renderer);
        }
    }
}

void MultiplayerManager::DrawImGuiMultiplayer(Renderer* renderer, Camera* camera) {
#ifndef ENGINE_RELEASE
    ImGui::SetNextWindowSize(ImVec2(480, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(" Multiplayer & Dedicated Server", nullptr)) {
        ImGui::TextColored(ImVec4(0.2f, 0.85f, 1.0f, 1.0f), "SERVER-AUTHORITATIVE DIRECT CONNECT");
        ImGui::Separator();

        if (!m_client.IsConnected()) {
            ImGui::InputText("Server IP Address", m_ipInputBuf, sizeof(m_ipInputBuf));
            ImGui::InputInt("Server port", &m_portInput);
            const auto configured=m_client.GetTlsOptions();m_useTls=configured.enabled;
            std::snprintf(m_tlsServerName,sizeof(m_tlsServerName),"%s",configured.serverName.c_str());
            std::snprintf(m_tlsCaFile,sizeof(m_tlsCaFile),"%s",configured.caCertificate.c_str());
            ImGui::BeginDisabled(m_client.IsConnecting());
            ImGui::Checkbox("TLS 1.3 (QUIC)",&m_useTls);
            if(m_useTls){
                ImGui::TextWrapped("Requires a TLS-enabled server and a trusted server certificate.");
                if(ImGui::CollapsingHeader("TLS certificate options")){
                    ImGui::InputText("Certificate server name (optional)",m_tlsServerName,sizeof(m_tlsServerName));
                    ImGui::InputText("Private CA PEM (optional)",m_tlsCaFile,sizeof(m_tlsCaFile));
                }
            }else ImGui::TextColored(ImVec4(1,.6f,.2f,1),"Legacy UDP: traffic is not encrypted.");
            ImGui::EndDisabled();
            TlsOptions tls;tls.enabled=m_useTls;tls.serverName=m_tlsServerName;tls.caCertificate=m_tlsCaFile;m_client.SetTlsOptions(tls);
            ImGui::InputText("Player Name", m_nameInputBuf, sizeof(m_nameInputBuf));

            ImGui::Spacing();
            if (m_client.IsConnecting()) {
                ImGui::Button(" Connecting to server...", ImVec2(-1, 36));
                if (ImGui::Button("Cancel", ImVec2(-1, 26))) {
                    m_client.Disconnect("User cancelled connection");
                }
            } else {
                if (ImGui::Button(" CONNECT TO SERVER", ImVec2(-1, 38))) {
                    ConnectToServer(m_ipInputBuf, static_cast<uint16_t>(m_portInput), m_nameInputBuf);
                }

                ImGui::Spacing();
                ImGui::SeparatorText("Quick LAN / Localhost Presets");
                if (ImGui::Button("Localhost (127.0.0.1:7777)")) {
                    strcpy_s(m_ipInputBuf, "127.0.0.1");
                    m_portInput = DEFAULT_PORT;
                    ConnectToServer(m_ipInputBuf, static_cast<uint16_t>(m_portInput), m_nameInputBuf);
                }
            }

            if (m_client.GetState() == ConnectionState::Failed || m_client.GetState() == ConnectionState::Timeout) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Status: %s", m_client.GetDisconnectReason().c_str());
            }
        } else {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "● Connected to %s", m_client.GetServerAddressString().c_str());
            ImGui::Text("Client ID: #%u | Network ID: #%u", m_client.GetLocalClientId(), m_client.GetLocalNetworkId());
            ImGui::TextUnformatted(m_client.IsTlsActive()?"Transport: QUIC / TLS 1.3":"Transport: legacy UDP (unencrypted)");
            ImGui::Text("Ping: %.1f ms | Players in match: %zu", m_client.GetPingMs(), GetConnectedPlayerCount());

            ImGui::Spacing();
            if (ImGui::Button(" DISCONNECT", ImVec2(160, 30))) {
                Disconnect();
            }

            ImGui::Spacing();
            ImGui::SeparatorText("Player List in World");
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), " You (ID #%u, %s) - Ping: %.1fms",
                m_client.GetLocalClientId(), m_nameInputBuf, m_client.GetPingMs());

            for (const auto& [netId, p] : m_client.GetRemotePlayers()) {
                DirectX::XMFLOAT3 col = GetPlayerPaletteColor(p.clientId);
                ImGui::TextColored(ImVec4(col.x, col.y, col.z, 1.0f),
                    "● Player #%u: %s (NetID: %u) - Pos: (%.1f, %.1f, %.1f) - Ping: %.1fms",
                    p.clientId, p.playerName.c_str(), netId,
                    p.latestState.position.x, p.latestState.position.y, p.latestState.position.z,
                    p.pingMs);
            }

            ImGui::Spacing();
            ImGui::SeparatorText("Multiplayer Chat");
            ImGui::BeginChild("ChatLogs", ImVec2(0, 100), true);
            for (const auto& msg : m_chatHistory) {
                ImGui::TextUnformatted(msg.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();

            ImGui::SetNextItemWidth(340);
            if (ImGui::InputText("##ChatInput", m_chatInputBuf, sizeof(m_chatInputBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (strlen(m_chatInputBuf) > 0) {
                    SendChatMessage(m_chatInputBuf);
                    m_chatInputBuf[0] = '\0';
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Send")) {
                if (strlen(m_chatInputBuf) > 0) {
                    SendChatMessage(m_chatInputBuf);
                    m_chatInputBuf[0] = '\0';
                }
            }
        }
    }
    ImGui::End();
#endif
}

} // namespace EngineNetwork
