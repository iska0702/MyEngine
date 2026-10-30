#pragma once

#include <string>
#include <memory>
#include <cstdint>

namespace EngineDiscord {

struct DiscordUserInfo {
    bool isConnected = false;
    bool isReady = false;
    uint64_t userId = 0;
    std::string username = "";
    std::string displayName = "";
    std::string globalName = "";
    std::string statusString = "Disconnected";
    std::string avatarHash = "";
};

struct DiscordPresenceData {
    std::string state = "";
    std::string details = "";
    std::string largeImage = "logo";
    std::string largeText = "Idontknow";
    std::string smallImage = "";
    std::string smallText = "";
    int partySize = 1;
    int partyMax = 4;
    std::string partyId = "";
    std::string joinSecret = "";
    bool hasParty = false;
    bool hasTimestamps = true;
};

class DiscordSystem {
public:
    static DiscordSystem& Get();

    // Initialize Discord Social SDK
    bool Initialize(uint64_t appId, const std::string& publicKey = "");
    void Shutdown();

    // Call once per frame in main engine loop
    void Update(float dt);

    // Rich Presence Controls
    void SetActivity(const std::string& state, const std::string& details);
    void SetParty(int currentSize, int maxSize, const std::string& partyId = "", const std::string& joinSecret = "");
    void ClearParty();
    void UpdatePresence();

    // User & Connection Info
    bool IsActive() const { return m_initialized; }
    bool IsReady() const { return m_userInfo.isReady; }
    const DiscordUserInfo& GetUserInfo() const { return m_userInfo; }
    const DiscordPresenceData& GetPresenceData() const { return m_presence; }
    uint64_t GetAppId() const { return m_appId; }
    const std::string& GetPublicKey() const { return m_publicKey; }

    // ImGui Discord Social Studio Window (Editor Build)
    void DrawUI();
    bool IsUIOpen() const { return m_showUI; }
    void SetUIOpen(bool open) { m_showUI = open; }
    void ToggleUI() { m_showUI = !m_showUI; }

private:
    DiscordSystem();
    ~DiscordSystem();
    DiscordSystem(const DiscordSystem&) = delete;
    DiscordSystem& operator=(const DiscordSystem&) = delete;

    bool m_initialized = false;
    uint64_t m_appId = 0;
    std::string m_publicKey;
    DiscordUserInfo m_userInfo;
    DiscordPresenceData m_presence;

    uint64_t m_startTimestamp = 0;
    float m_presenceUpdateTimer = 0.0f;
    bool m_showUI = false;
    bool m_needsPresenceUpdate = false;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace EngineDiscord
