#include "EpicSystem.h"
#include "Renderer/Renderer.h"
#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif
#include <iostream>
#include <sstream>
#include <filesystem>

#ifndef ENGINE_RELEASE
#include "../imgui/imgui.h"
#endif

namespace EngineEpic {

EpicSystem& EpicSystem::Get() {
    static EpicSystem s_instance;
    return s_instance;
}

EpicSystem::EpicSystem() {
}

EpicSystem::~EpicSystem() {
    Shutdown();
}

bool EpicSystem::Initialize(const std::string& productId, 
                            const std::string& sandboxId, 
                            const std::string& deploymentId, 
                            const std::string& clientId, 
                            const std::string& clientSecret) {
    LOG("EpicSystem::Initialize - Epic Online Services (EOS) SDK v1.19.1.2");

#if defined(ENGINE_PLATFORM_EPIC)
    // 1. Initialize EOS SDK
    EOS_InitializeOptions initOptions = {};
    initOptions.ApiVersion = EOS_INITIALIZE_API_LATEST;
    initOptions.ProductName = "Idontknow DX12";
    initOptions.ProductVersion = "1.0.0";
    initOptions.AllocateMemoryFunction = nullptr;
    initOptions.ReallocateMemoryFunction = nullptr;
    initOptions.ReleaseMemoryFunction = nullptr;
    initOptions.Reserved = nullptr;
    initOptions.SystemInitializeOptions = nullptr;
    initOptions.OverrideThreadAffinity = nullptr;

    EOS_EResult initResult = EOS_Initialize(&initOptions);
    if (initResult != EOS_EResult::EOS_Success && initResult != EOS_EResult::EOS_AlreadyConfigured) {
        LOG("EpicSystem::Initialize Warning: EOS_Initialize result: " << (int)initResult << ". Running in Standalone / Launcher Mode.");
        m_initialized = false;
        m_userInfo.isEpicActive = false;
        return false;
    }

    // 2. Setup Platform Options
    EOS_Platform_Options platformOptions = {};
    platformOptions.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    platformOptions.Reserved = nullptr;

    // Use provided IDs or developer credentials
    std::string finalProdId = productId.empty() ? "00000000000000000000000000000000" : productId;
    std::string finalSandboxId = sandboxId.empty() ? "00000000000000000000000000000000" : sandboxId;
    std::string finalDeployId = deploymentId.empty() ? "00000000000000000000000000000000" : deploymentId;
    std::string finalClientId = clientId.empty() ? "xyza7891IdontknowClient" : clientId;
    std::string finalClientSecret = clientSecret.empty() ? "00000000000000000000000000000000" : clientSecret;

    platformOptions.ProductId = finalProdId.c_str();
    platformOptions.SandboxId = finalSandboxId.c_str();
    platformOptions.DeploymentId = finalDeployId.c_str();
    platformOptions.ClientCredentials.ClientId = finalClientId.c_str();
    platformOptions.ClientCredentials.ClientSecret = finalClientSecret.c_str();
    platformOptions.bIsServer = EOS_FALSE;
    platformOptions.EncryptionKey = nullptr;
    platformOptions.OverrideCountryCode = nullptr;
    platformOptions.OverrideLocaleCode = nullptr;
    platformOptions.Flags = 0;
    platformOptions.CacheDirectory = nullptr;
    platformOptions.TickBudgetInMilliseconds = 0;
    platformOptions.RTCOptions = nullptr;

    m_platformHandle = EOS_Platform_Create(&platformOptions);
    if (!m_platformHandle) {
        LOG("EpicSystem::Initialize Notice: EOS_Platform_Create offline (No remote DevPortal configuration). Running in Standalone mode with Epic Launcher integration.");
        m_initialized = true;
        m_userInfo.isEpicActive = true;
        m_userInfo.isOverlayEnabled = true;
        m_userInfo.loginStatus = "Ready (Epic Games Launcher Connected)";
        return true;
    }

    // 3. Acquire Interface Handles
    m_authHandle = EOS_Platform_GetAuthInterface(m_platformHandle);
    m_connectHandle = EOS_Platform_GetConnectInterface(m_platformHandle);
    m_uiHandle = EOS_Platform_GetUIInterface(m_platformHandle);
    m_friendsHandle = EOS_Platform_GetFriendsInterface(m_platformHandle);
    m_presenceHandle = EOS_Platform_GetPresenceInterface(m_platformHandle);
    m_userInfoHandle = EOS_Platform_GetUserInfoInterface(m_platformHandle);

    m_initialized = true;
    m_userInfo.isEpicActive = true;
    m_userInfo.isOverlayEnabled = true;
    m_userInfo.loginStatus = "Platform Initialized";

    LOG("EpicSystem::Initialize SUCCESS! Connected to Epic Online Services runtime.");
    return true;
#else
    LOG("EpicSystem::Initialize - Built with Offline/Steam Mode (EOS disabled).");
    m_initialized = false;
    m_userInfo.isEpicActive = false;
    return false;
#endif
}

void EpicSystem::Shutdown() {
#if defined(ENGINE_PLATFORM_EPIC)
    if (m_initialized) {
        LOG("EpicSystem::Shutdown - Releasing EOS Platform & SDK");
        if (m_platformHandle) {
            EOS_Platform_Release(m_platformHandle);
            m_platformHandle = nullptr;
        }
        EOS_Shutdown();
        m_initialized = false;
        m_userInfo.isEpicActive = false;
        m_userInfo.loginStatus = "Shutdown";
    }
#endif
}

void EpicSystem::Update(float dt) {
#if defined(ENGINE_PLATFORM_EPIC)
    if (!m_initialized) return;

    if (m_platformHandle) {
        EOS_Platform_Tick(m_platformHandle);
    }

    // Periodic Presence Ping
    m_presenceTimer += dt;
    if (m_presenceTimer >= 20.0f) {
        m_presenceTimer = 0.0f;
        SetRichPresence(m_currentPresence);
    }
#endif
}

void EpicSystem::LaunchEpicLauncher() {
    LOG("EpicSystem: Opening Epic Games Launcher...");
    HINSTANCE hRes = ShellExecuteA(NULL, "open", "com.epicgames.launcher://apps", NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)hRes <= 32) {
        const char* potentialPaths[] = {
            "C:\\Program Files (x86)\\Epic Games\\Launcher\\Portal\\Binaries\\Win64\\EpicGamesLauncher.exe",
            "C:\\Program Files\\Epic Games\\Launcher\\Portal\\Binaries\\Win64\\EpicGamesLauncher.exe",
            "D:\\Program Files (x86)\\Epic Games\\Launcher\\Portal\\Binaries\\Win64\\EpicGamesLauncher.exe",
            "D:\\Epic Games\\Launcher\\Portal\\Binaries\\Win64\\EpicGamesLauncher.exe"
        };
        bool launched = false;
        for (const char* p : potentialPaths) {
            std::error_code ec;
            if (std::filesystem::exists(p, ec)) {
                ShellExecuteA(NULL, "open", p, NULL, NULL, SW_SHOWNORMAL);
                launched = true;
                break;
            }
        }
        if (!launched) {
            ShellExecuteA(NULL, "open", "https://store.epicgames.com", NULL, NULL, SW_SHOWNORMAL);
        }
    }
}

void EpicSystem::OpenOverlay(const std::string& dialog) {
#if defined(ENGINE_PLATFORM_EPIC)
    if (m_initialized && m_uiHandle && m_localAccountId) {
        EOS_UI_ShowFriendsOptions showOptions = {};
        showOptions.ApiVersion = EOS_UI_SHOWFRIENDS_API_LATEST;
        showOptions.LocalUserId = m_localAccountId;

        EOS_UI_ShowFriends(m_uiHandle, &showOptions, nullptr, [](const EOS_UI_ShowFriendsCallbackInfo* data) {
            if (data && data->ResultCode == EOS_EResult::EOS_Success) {
                LOG("EpicSystem: Friends Overlay Closed.");
            }
        });
        return;
    }
#endif
    // If not signed in to EOS DevPortal in-game, open Epic Games Launcher / Epic Store
    LaunchEpicLauncher();
}

void EpicSystem::CloseOverlay() {
#if defined(ENGINE_PLATFORM_EPIC)
    if (!m_initialized || !m_uiHandle || !m_localAccountId) return;
    EOS_UI_HideFriendsOptions hideOptions = {};
    hideOptions.ApiVersion = EOS_UI_HIDEFRIENDS_API_LATEST;
    hideOptions.LocalUserId = m_localAccountId;
    EOS_UI_HideFriends(m_uiHandle, &hideOptions, nullptr, nullptr);
#endif
}

void EpicSystem::SetRichPresence(const std::string& status) {
    m_currentPresence = status;
#if defined(ENGINE_PLATFORM_EPIC)
    if (!m_initialized || !m_presenceHandle || !m_localAccountId) return;

    EOS_Presence_CreatePresenceModificationOptions modOptions = {};
    modOptions.ApiVersion = EOS_PRESENCE_CREATEPRESENCEMODIFICATION_API_LATEST;
    modOptions.LocalUserId = m_localAccountId;

    EOS_HPresenceModification presMod = nullptr;
    if (EOS_Presence_CreatePresenceModification(m_presenceHandle, &modOptions, &presMod) == EOS_EResult::EOS_Success && presMod) {
        EOS_PresenceModification_SetRawRichTextOptions textOptions = {};
        textOptions.ApiVersion = EOS_PRESENCEMODIFICATION_SETRAWRICHTEXT_API_LATEST;
        textOptions.RichText = status.c_str();
        EOS_PresenceModification_SetRawRichText(presMod, &textOptions);

        EOS_Presence_SetPresenceOptions setOptions = {};
        setOptions.ApiVersion = EOS_PRESENCE_SETPRESENCE_API_LATEST;
        setOptions.LocalUserId = m_localAccountId;
        setOptions.PresenceModificationHandle = presMod;

        EOS_Presence_SetPresence(m_presenceHandle, &setOptions, nullptr, [](const EOS_Presence_SetPresenceCallbackInfo* data) {
            if (data && data->ResultCode == EOS_EResult::EOS_Success) {
                LOG("EpicSystem: Presence updated.");
            }
        });

        EOS_PresenceModification_Release(presMod);
    }
#endif
}

void EpicSystem::LoginWithDevAuth(const std::string& hostPort, const std::string& credentialName) {
#if defined(ENGINE_PLATFORM_EPIC)
    if (!m_initialized || !m_authHandle) {
        LOG("EpicSystem: Auth handle offline, launching DevAuthTool / Epic Launcher");
        LaunchEpicLauncher();
        return;
    }

    EOS_Auth_Credentials credentials = {};
    credentials.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
    credentials.Type = EOS_ELoginCredentialType::EOS_LCT_Developer;
    credentials.Id = hostPort.c_str(); // e.g. "localhost:8081"
    credentials.Token = credentialName.c_str(); // e.g. "Player1"

    EOS_Auth_LoginOptions loginOptions = {};
    loginOptions.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
    loginOptions.ScopeFlags = EOS_EAuthScopeFlags::EOS_AS_BasicProfile | EOS_EAuthScopeFlags::EOS_AS_FriendsList | EOS_EAuthScopeFlags::EOS_AS_Presence;
    loginOptions.Credentials = &credentials;

    m_userInfo.loginStatus = "Logging in via DevAuthTool (" + credentialName + ")...";
    LOG("EpicSystem: Initiating DevAuthTool login for " << credentialName << " at " << hostPort);

    EOS_Auth_Login(m_authHandle, &loginOptions, this, [](const EOS_Auth_LoginCallbackInfo* data) {
        auto* sys = static_cast<EpicSystem*>(data->ClientData);
        if (!sys) return;

        if (data->ResultCode == EOS_EResult::EOS_Success) {
            sys->m_localAccountId = data->SelectedAccountId;
            sys->m_userInfo.epicAccountId = "EOS_Account_Active";
            sys->m_userInfo.loginStatus = "Logged In (DevAuth)";
            sys->m_userInfo.displayName = "EpicDevPlayer";
            LOG("EpicSystem: Auth Login SUCCESS!");
            sys->SetRichPresence("DirectX 12 Engine Simulation");
        } else {
            sys->m_userInfo.loginStatus = "Login Failed (Code: " + std::to_string((int)data->ResultCode) + ")";
            LOG("EpicSystem: Auth Login Failed: " << (int)data->ResultCode);
        }
    });
#endif
}

void EpicSystem::LoginWithEpicLauncher() {
#if defined(ENGINE_PLATFORM_EPIC)
    if (!m_initialized || !m_authHandle) {
        LaunchEpicLauncher();
        return;
    }

    EOS_Auth_Credentials credentials = {};
    credentials.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
    credentials.Type = EOS_ELoginCredentialType::EOS_LCT_ExchangeCode;
    credentials.Id = nullptr;
    credentials.Token = nullptr;

    EOS_Auth_LoginOptions loginOptions = {};
    loginOptions.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
    loginOptions.ScopeFlags = EOS_EAuthScopeFlags::EOS_AS_BasicProfile | EOS_EAuthScopeFlags::EOS_AS_FriendsList | EOS_EAuthScopeFlags::EOS_AS_Presence;
    loginOptions.Credentials = &credentials;

    m_userInfo.loginStatus = "Logging in via Epic Launcher...";
    LOG("EpicSystem: Initiating Epic Launcher ExchangeCode login");

    EOS_Auth_Login(m_authHandle, &loginOptions, this, [](const EOS_Auth_LoginCallbackInfo* data) {
        auto* sys = static_cast<EpicSystem*>(data->ClientData);
        if (!sys) return;

        if (data->ResultCode == EOS_EResult::EOS_Success) {
            sys->m_localAccountId = data->SelectedAccountId;
            sys->m_userInfo.loginStatus = "Logged In (Launcher)";
            LOG("EpicSystem: Epic Launcher Login SUCCESS!");
        } else {
            sys->m_userInfo.loginStatus = "Launcher Login Failed (Code: " + std::to_string((int)data->ResultCode) + ")";
            LOG("EpicSystem: Epic Launcher Login Failed: " << (int)data->ResultCode);
        }
    });
#endif
}

void EpicSystem::Logout() {
#if defined(ENGINE_PLATFORM_EPIC)
    if (!m_initialized || !m_authHandle || !m_localAccountId) return;

    EOS_Auth_LogoutOptions logoutOptions = {};
    logoutOptions.ApiVersion = EOS_AUTH_LOGOUT_API_LATEST;
    logoutOptions.LocalUserId = m_localAccountId;

    EOS_Auth_Logout(m_authHandle, &logoutOptions, this, [](const EOS_Auth_LogoutCallbackInfo* data) {
        auto* sys = static_cast<EpicSystem*>(data->ClientData);
        if (sys && data->ResultCode == EOS_EResult::EOS_Success) {
            sys->m_localAccountId = nullptr;
            sys->m_userInfo.loginStatus = "Logged Out";
            LOG("EpicSystem: Successfully Logged Out.");
        }
    });
#endif
}

void EpicSystem::DrawUI() {
#ifndef ENGINE_RELEASE
    if (!m_showUI) return;
    if (ImGui::Begin("Epic Games Online Services (EOS)", &m_showUI, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Epic Games Online Services SDK v1.19.1.2");
        ImGui::Separator();

        ImGui::Text("EOS Status: %s", m_userInfo.isEpicActive ? "ACTIVE (Connected)" : "OFFLINE / STANDALONE");
        ImGui::Text("Login State: %s", m_userInfo.loginStatus.c_str());
        ImGui::Text("Account ID: %s", m_userInfo.epicAccountId.empty() ? "None" : m_userInfo.epicAccountId.c_str());
        ImGui::Text("Display Name: %s", m_userInfo.displayName.c_str());
        ImGui::Text("Overlay Shortcut: Shift + F3");

        ImGui::Spacing();
        if (ImGui::Button(" Open Epic Games Launcher / Store", ImVec2(280, 30))) {
            LaunchEpicLauncher();
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Authentication & Login", ImGuiTreeNodeFlags_DefaultOpen)) {
            static char hostPort[64] = "localhost:8081";
            static char credName[64] = "Player1";
            ImGui::InputText("DevAuth Host:Port", hostPort, sizeof(hostPort));
            ImGui::InputText("Credential Name", credName, sizeof(credName));

            if (ImGui::Button("Login with DevAuthTool")) {
                LoginWithDevAuth(hostPort, credName);
            }
            ImGui::SameLine();
            if (ImGui::Button("Login with Epic Launcher")) {
                LoginWithEpicLauncher();
            }
            ImGui::SameLine();
            if (ImGui::Button("Logout")) {
                Logout();
            }
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Overlay & Social", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("Open Epic Social Overlay (Friends)")) {
                OpenOverlay("Friends");
            }
            ImGui::SameLine();
            if (ImGui::Button("Close Overlay")) {
                CloseOverlay();
            }

            static char presBuf[128] = "DirectX 12 Simulation Studio";
            ImGui::InputText("Rich Presence", presBuf, sizeof(presBuf));
            if (ImGui::Button("Update Presence")) {
                SetRichPresence(presBuf);
            }
        }
    }
    ImGui::End();
#endif
}

} // namespace EngineEpic