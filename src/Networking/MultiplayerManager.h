#pragma once

#include "NetworkTypes.h"
#include "NetworkClient.h"
#include <string>
#include <memory>
#include <vector>

class Renderer;
class Camera;
class Window;

namespace EngineNetwork {

class MultiplayerManager {
public:
    static MultiplayerManager& Get();

    void Initialize();
    void Shutdown();

    bool ConnectToServer(const std::string& address, uint16_t port = DEFAULT_PORT, const std::string& playerName = "Player");
    void Disconnect();

    void Update(float dt, Renderer* renderer, Camera* camera, Window* window);

    bool IsMultiplayerActive() const;
    bool IsConnected() const;
    bool IsConnecting() const;

    NetworkClient& GetClient() { return m_client; }
    const NetworkClient& GetClient() const { return m_client; }

    float GetPingMs() const { return m_client.GetPingMs(); }
    uint32_t GetLocalClientId() const { return m_client.GetLocalClientId(); }
    uint32_t GetLocalNetworkId() const { return m_client.GetLocalNetworkId(); }
    std::string GetServerAddressString() const { return m_client.GetServerAddressString(); }
    std::string GetConnectionStateString() const;
    size_t GetConnectedPlayerCount() const;

    // Chat
    void SendChatMessage(const std::string& message);
    const std::vector<std::string>& GetRecentChatLogs() const { return m_chatHistory; }

    // Fallback UI drawing
    void DrawImGuiMultiplayer(Renderer* renderer, Camera* camera);

private:
    MultiplayerManager();
    ~MultiplayerManager();
    MultiplayerManager(const MultiplayerManager&) = delete;
    MultiplayerManager& operator=(const MultiplayerManager&) = delete;

    void SyncRemotePlayerVisuals(Renderer* renderer);
    void CleanupRemotePlayerVisuals(Renderer* renderer);

    NetworkClient m_client;
    double m_elapsedTime = 0.0;

    // Visual model IDs in renderer mapped to remote player networkId
    std::unordered_map<uint32_t, uint32_t> m_remoteModelIds;
    std::vector<std::string> m_chatHistory;

    char m_ipInputBuf[128] = "127.0.0.1";
    int m_portInput = DEFAULT_PORT;
    char m_nameInputBuf[64] = "Player";
    char m_chatInputBuf[256] = "";
    bool m_wasConnected = false;
    bool m_useTls=true;
    char m_tlsServerName[256]="";
    char m_tlsCaFile[512]="";
};

} // namespace EngineNetwork
