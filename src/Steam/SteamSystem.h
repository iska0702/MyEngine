#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace EngineSteam {

struct SteamUserInfo {
    bool isSteamActive = false;
    bool isSubscribed = false;
    bool isVACBanned = false;
    bool isCybercafe = false;
    bool isLowViolence = false;
    bool isOverlayEnabled = false;
    uint32_t appId = 480;
    uint64_t steamId64 = 0;
    uint32_t accountId = 0;
    std::string personaName = "Standalone User";
    std::string language = "english";
    std::string installDir = "";
    int dlcCount = 0;
};

class SteamSystem {
public:
    static SteamSystem& Get();

    // Initialize Steamworks API with DRM protection checks
    bool Initialize(uint32_t defaultAppId = 480, bool enforceDrmRestart = false);
    void Shutdown();

    // Call every frame in main engine loop
    void Update(float dt);

    // Protection & DRM Verification
    bool IsActive() const { return m_initialized; }
    bool VerifyLicense();
    const SteamUserInfo& GetUserInfo() const { return m_userInfo; }

    // User Identity
    std::string GetPersonaName() const { return m_userInfo.personaName; }
    uint64_t GetSteamID64() const { return m_userInfo.steamId64; }
    uint32_t GetAppID() const { return m_userInfo.appId; }
    std::string GetLanguage() const { return m_userInfo.language; }

    // Rich Presence
    void SetRichPresence(const std::string& key, const std::string& value);
    void ClearRichPresence();

    // Achievements & Stats
    bool UnlockAchievement(const std::string& achievementName);
    bool SetStat(const std::string& statName, int32_t value);
    bool SetStat(const std::string& statName, float value);
    bool StoreStats();

    // Steam Overlay
    void OpenOverlay(const std::string& dialog = "");
    void SetOverlayActive(bool active);
    bool IsOverlayActive() const { return m_overlayActive; }

    // ImGui Steam & DRM Security Window
    void DrawUI();
    bool IsUIOpen() const { return m_showUI; }
    void SetUIOpen(bool open) { m_showUI = open; }
    void ToggleUI() { m_showUI = !m_showUI; }

private:
    SteamSystem();
    ~SteamSystem();
    SteamSystem(const SteamSystem&) = delete;
    SteamSystem& operator=(const SteamSystem&) = delete;

    bool m_initialized = false;
    bool m_overlayActive = false;
    SteamUserInfo m_userInfo;
    float m_richPresenceTimer = 0.0f;
    bool m_showUI = false;

    void EnsureAppIdFile(uint32_t appId);
    void RefreshUserInfo();
};

} // namespace EngineSteam
