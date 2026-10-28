#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>

#if defined(ENGINE_PLATFORM_EPIC)
#include "eos_sdk.h"
#include "eos_init.h"
#include "eos_auth.h"
#include "eos_connect.h"
#include "eos_ui.h"
#include "eos_friends.h"
#include "eos_presence.h"
#include "eos_userinfo.h"
#endif

namespace EngineEpic {

struct EpicUserInfo {
    bool isEpicActive = false;
    bool isOverlayEnabled = false;
    std::string epicAccountId = "";
    std::string productUserId = "";
    std::string displayName = "Standalone Epic User";
    std::string country = "US";
    std::string preferredLanguage = "en";
    std::string loginStatus = "Not Logged In";
};

class EpicSystem {
public:
    static EpicSystem& Get();

    bool Initialize(const std::string& productId = "", 
                    const std::string& sandboxId = "", 
                    const std::string& deploymentId = "", 
                    const std::string& clientId = "", 
                    const std::string& clientSecret = "");
    void Shutdown();
    void Update(float dt);

    // EOS In-Game Social Overlay & Epic Launcher
    void OpenOverlay(const std::string& dialog = "Friends");
    void CloseOverlay();
    void LaunchEpicLauncher();

    // Epic Rich Presence
    void SetRichPresence(const std::string& status);

    // Epic Authentication (DevAuthTool / Epic Games Launcher)
    void LoginWithDevAuth(const std::string& hostPort = "localhost:8081", const std::string& credentialName = "Player1");
    void LoginWithEpicLauncher();
    void Logout();

    // User profile & Status
    const EpicUserInfo& GetUserInfo() const { return m_userInfo; }
    bool IsInitialized() const { return m_initialized; }

    // ImGui Control Studio (Editor Build only)
    void DrawUI();
    bool IsUIOpen() const { return m_showUI; }
    void SetUIOpen(bool open) { m_showUI = open; }
    void ToggleUI() { m_showUI = !m_showUI; }

private:
    EpicSystem();
    ~EpicSystem();

    bool m_initialized = false;
    bool m_showUI = false;
    EpicUserInfo m_userInfo;
    float m_presenceTimer = 0.0f;
    std::string m_currentPresence = "Exploring DX12 Engine World";

#if defined(ENGINE_PLATFORM_EPIC)
    EOS_HPlatform m_platformHandle = nullptr;
    EOS_HAuth m_authHandle = nullptr;
    EOS_HConnect m_connectHandle = nullptr;
    EOS_HUI m_uiHandle = nullptr;
    EOS_HFriends m_friendsHandle = nullptr;
    EOS_HPresence m_presenceHandle = nullptr;
    EOS_HUserInfo m_userInfoHandle = nullptr;
    EOS_EpicAccountId m_localAccountId = nullptr;
    EOS_ProductUserId m_localProductUserId = nullptr;
#endif
};

} // namespace EngineEpic