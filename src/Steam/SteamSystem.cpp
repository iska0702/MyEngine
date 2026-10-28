#include "SteamSystem.h"
#if defined(_WIN32)
#include <windows.h>
#endif
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>

#ifndef ENGINE_RELEASE
#include <imgui.h>
#endif

#if defined(ENGINE_PLATFORM_STEAM)
#include <steam/steam_api.h>
#endif

#include "../UI/WebMenuSystem.h"

#define LOG(x) std::cout << "[Idontknow] " << x << std::endl

namespace EngineSteam {

#if defined(ENGINE_PLATFORM_STEAM)
class SteamCallbackHandler {
public:
    STEAM_CALLBACK(SteamCallbackHandler, OnGameOverlayActivated, GameOverlayActivated_t);
};

void SteamCallbackHandler::OnGameOverlayActivated(GameOverlayActivated_t* pCallback) {
    if (pCallback) {
        bool active = (pCallback->m_bActive != 0);
        LOG("Steam Game Overlay Active: " << (active ? "TRUE" : "FALSE"));
        SteamSystem::Get().SetOverlayActive(active);
    }
}

static std::unique_ptr<SteamCallbackHandler> s_steamCallbackHandler;
#endif

SteamSystem& SteamSystem::Get() {
    static SteamSystem s_instance;
    return s_instance;
}

SteamSystem::SteamSystem() {
}

SteamSystem::~SteamSystem() {
    Shutdown();
}

#if defined(_WIN32)
#include <tlhelp32.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

static bool IsSteamClientProcessRunning() {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnapshot, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"steam.exe") == 0) {
                    CloseHandle(hSnapshot);
                    return true;
                }
            } while (Process32NextW(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);
    }
    return false;
}

static bool LaunchSteamClientExecutable() {
    // 1. Query registry HKCU\Software\Valve\Steam\SteamExe
    HKEY hKey = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char steamExe[MAX_PATH] = {};
        DWORD dwSize = sizeof(steamExe);
        DWORD dwType = 0;
        if (RegQueryValueExA(hKey, "SteamExe", nullptr, &dwType, reinterpret_cast<LPBYTE>(steamExe), &dwSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            for (int i = 0; steamExe[i]; ++i) {
                if (steamExe[i] == '/') steamExe[i] = '\\';
            }
            HINSTANCE res = ShellExecuteA(nullptr, "open", steamExe, nullptr, nullptr, SW_SHOW);
            if (reinterpret_cast<uintptr_t>(res) > 32) {
                return true;
            }
        } else {
            RegCloseKey(hKey);
        }
    }

    // 2. Fallback via URL protocol steam://
    HINSTANCE res = ShellExecuteA(nullptr, "open", "steam://open/main", nullptr, nullptr, SW_SHOW);
    return (reinterpret_cast<uintptr_t>(res) > 32);
}
#else
static bool IsSteamClientProcessRunning() {
    return true;
}
static bool LaunchSteamClientExecutable() {
    return true;
}
#endif

void SteamSystem::EnsureAppIdFile(uint32_t appId) {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();

    std::string appIdStr = std::to_string(appId);
    SetEnvironmentVariableA("SteamAppId", appIdStr.c_str());
    SetEnvironmentVariableA("SteamGameId", appIdStr.c_str());

    std::error_code ec;
    std::filesystem::remove(exeDir / "steam_appid.txt", ec);
    std::filesystem::remove(exeDir / "ThirdParty" / "Steamworks" / "steam_appid.txt", ec);
    std::filesystem::remove(exeDir / "ThirdParty" / "steam_appid.txt", ec);
    std::filesystem::remove(exeDir.parent_path() / "steam_appid.txt", ec);

    std::filesystem::path nestedBuild = exeDir / "build";
    if (std::filesystem::exists(nestedBuild, ec)) {
        std::filesystem::remove_all(nestedBuild, ec);
    }
}

bool SteamSystem::Initialize(uint32_t defaultAppId, bool enforceDrmRestart) {
#if defined(ENGINE_PLATFORM_STEAM)
    LOG("SteamSystem::Initialize - Steamworks enabled");
    EnsureAppIdFile(defaultAppId);
    // 1. Steam Client Auto-Launch Check (if Steam is closed in Release mode)
    if (enforceDrmRestart && !IsSteamClientProcessRunning()) {
        LOG("SteamSystem::DRM - Steam Client is closed. Launching Steam.exe...");
        if (LaunchSteamClientExecutable()) {
            LOG("SteamSystem::DRM - Steam Client launched! Waiting for Steam to initialize...");
            // Wait up to 15 seconds for steam.exe process to appear
            for (int i = 0; i < 30; ++i) {
                Sleep(500);
                if (IsSteamClientProcessRunning()) {
                    Sleep(2000); // Give Steam client 2 seconds to initialize internal pipes
                    break;
                }
            }
        }
    }

    // 2. Initialize Steamworks API
    SteamErrMsg errMsg = "";
    ESteamAPIInitResult initResult = SteamAPI_InitEx(&errMsg);
    if (initResult != k_ESteamAPIInitResult_OK) {
        LOG("SteamSystem::InitEx status: " << (int)initResult << " (" << errMsg << "). Trying SteamAPI_Init...");
        
        bool connected = false;
        int maxRetries = enforceDrmRestart ? 20 : 1;
        for (int r = 0; r < maxRetries; ++r) {
            if (SteamAPI_Init()) {
                connected = true;
                break;
            }
            if (enforceDrmRestart) {
                Sleep(500);
            }
        }

        if (!connected) {
            LOG("SteamSystem::Init Warning: Steam Client is not connected. Running in Standalone Offline Mode.");
            m_initialized = false;
            m_userInfo.isSteamActive = false;
            m_userInfo.appId = defaultAppId;
            return false;
        }
    }

    m_initialized = true;
    m_userInfo.isSteamActive = true;
    s_steamCallbackHandler = std::make_unique<SteamCallbackHandler>();
    LOG("SteamSystem::Init SUCCESS! Connected to Steamworks runtime.");

    // 3. Query & Cache User Profile & License Info
    RefreshUserInfo();

    // 4. Set Initial Rich Presence
    SetRichPresence("status", "Exploring Next-Gen DX12 World");
    SetRichPresence("steam_display", "#Status_InGame");

    // 5. Request User Stats & Achievements
    if (SteamUserStats() && SteamUser()) {
        SteamUserStats()->RequestUserStats(SteamUser()->GetSteamID());
    }

    return true;
#else
    LOG("SteamSystem::Initialize - Steamworks disabled.");
    m_initialized = false;
    m_userInfo.isSteamActive = false;
    return false;
#endif
}

void SteamSystem::Shutdown() {
#if defined(ENGINE_PLATFORM_STEAM)
    if (m_initialized) {
        LOG("SteamSystem::Shutdown - Unloading Steamworks API");
        ClearRichPresence();
        s_steamCallbackHandler.reset();
        SteamAPI_Shutdown();
        m_initialized = false;
        m_userInfo.isSteamActive = false;
    }
#endif
}

void SteamSystem::SetOverlayActive(bool active) {
    m_overlayActive = active;
    EngineUI::WebMenuSystem::Get().SetOverlayOccluded(active);
}

void SteamSystem::RefreshUserInfo() {
#if defined(ENGINE_PLATFORM_STEAM)
    if (!m_initialized) return;

    if (SteamUtils()) {
        m_userInfo.appId = SteamUtils()->GetAppID();
        m_userInfo.isOverlayEnabled = SteamUtils()->IsOverlayEnabled();
    }

    if (SteamUser()) {
        CSteamID steamId = SteamUser()->GetSteamID();
        m_userInfo.steamId64 = steamId.ConvertToUint64();
        m_userInfo.accountId = steamId.GetAccountID();
    }

    if (SteamFriends()) {
        const char* name = SteamFriends()->GetPersonaName();
        if (name && strlen(name) > 0) {
            m_userInfo.personaName = name;
        }
    }

    if (SteamApps()) {
        m_userInfo.isSubscribed = SteamApps()->BIsSubscribed();
        m_userInfo.isVACBanned = SteamApps()->BIsVACBanned();
        m_userInfo.isCybercafe = SteamApps()->BIsCybercafe();
        m_userInfo.isLowViolence = SteamApps()->BIsLowViolence();
        m_userInfo.dlcCount = SteamApps()->GetDLCCount();

        const char* lang = SteamApps()->GetCurrentGameLanguage();
        if (lang && strlen(lang) > 0) {
            m_userInfo.language = lang;
        }

        char installDir[1024] = {};
        SteamApps()->GetAppInstallDir(m_userInfo.appId, installDir, sizeof(installDir));
        m_userInfo.installDir = installDir;
    }

    LOG("Steam Profile: " << m_userInfo.personaName 
        << " (SteamID64: " << m_userInfo.steamId64 
        << ", AppID: " << m_userInfo.appId 
        << ", Language: " << m_userInfo.language 
        << ", Subscribed: " << (m_userInfo.isSubscribed ? "YES" : "NO") << ")");
#endif
}

bool SteamSystem::VerifyLicense() {
#if defined(ENGINE_PLATFORM_STEAM)
    if (!m_initialized) return false;
    if (SteamApps()) {
        return SteamApps()->BIsSubscribed();
    }
#endif
    return false;
}

void SteamSystem::Update(float dt) {
#if defined(ENGINE_PLATFORM_STEAM)
    if (!m_initialized) return;

    // Run frame callbacks (achievements, friend status, overlay events)
    SteamAPI_RunCallbacks();

    // Periodic rich presence ping
    m_richPresenceTimer += dt;
    if (m_richPresenceTimer >= 15.0f) {
        m_richPresenceTimer = 0.0f;
        SetRichPresence("status", "DirectX 12 Next-Gen Physics Engine - Active");
    }
#endif
}

void SteamSystem::SetRichPresence(const std::string& key, const std::string& value) {
#if defined(ENGINE_PLATFORM_STEAM)
    if (!m_initialized) return;
    if (SteamFriends()) {
        SteamFriends()->SetRichPresence(key.c_str(), value.c_str());
    }
#endif
}

void SteamSystem::ClearRichPresence() {
#if defined(ENGINE_PLATFORM_STEAM)
    if (!m_initialized) return;
    if (SteamFriends()) {
        SteamFriends()->ClearRichPresence();
    }
#endif
}

bool SteamSystem::UnlockAchievement(const std::string& achievementName) {
#if defined(ENGINE_PLATFORM_STEAM)
    if (!m_initialized || !SteamUserStats()) return false;
    bool res = SteamUserStats()->SetAchievement(achievementName.c_str());
    if (res) {
        SteamUserStats()->StoreStats();
        LOG("Steam Achievement Unlocked: " << achievementName);
    }
    return res;
#else
    return false;
#endif
}

bool SteamSystem::SetStat(const std::string& statName, int32_t value) {
#if defined(ENGINE_PLATFORM_STEAM)
    if (!m_initialized || !SteamUserStats()) return false;
    return SteamUserStats()->SetStat(statName.c_str(), value);
#else
    return false;
#endif
}

bool SteamSystem::SetStat(const std::string& statName, float value) {
#if defined(ENGINE_PLATFORM_STEAM)
    if (!m_initialized || !SteamUserStats()) return false;
    return SteamUserStats()->SetStat(statName.c_str(), value);
#else
    return false;
#endif
}

bool SteamSystem::StoreStats() {
#if defined(ENGINE_PLATFORM_STEAM)
    if (!m_initialized || !SteamUserStats()) return false;
    return SteamUserStats()->StoreStats();
#else
    return false;
#endif
}

void SteamSystem::OpenOverlay(const std::string& dialog) {
#if defined(ENGINE_PLATFORM_STEAM)
    if (!m_initialized || !SteamFriends()) return;
    if (dialog.empty()) {
        SteamFriends()->ActivateGameOverlay(nullptr);
    } else {
        SteamFriends()->ActivateGameOverlay(dialog.c_str());
    }
#endif
}

void SteamSystem::DrawUI() {
#ifndef ENGINE_RELEASE
#if defined(ENGINE_PLATFORM_STEAM)
    if (!m_showUI) return;

    ImGui::SetNextWindowSize(ImVec2(540, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Steam & DRM Protection Studio (SDK 1.65)", &m_showUI)) {
        if (m_initialized) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.4f, 1.0f), "STEAMWORKS API ACTIVE (Connected)");
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "DLL Location: ThirdParty/Steamworks/steam_api64.dll");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "STEAMWORKS OFFLINE (Standalone mode)");
            ImGui::TextWrapped("Steam client is not running or not logged in. Make sure Steam is running in the background, then click 'Reconnect to Steam'.");
            
            if (ImGui::Button("Reconnect to Steam")) {
                Initialize(m_userInfo.appId, false);
            }
        }
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Player Identity & Account", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Persona Name: %s", m_userInfo.personaName.c_str());
            ImGui::Text("SteamID64: %llu", m_userInfo.steamId64);
            ImGui::Text("Account ID: %u", m_userInfo.accountId);
            ImGui::Text("Active AppID: %u", m_userInfo.appId);
            ImGui::Text("Game Language: %s", m_userInfo.language.c_str());
            if (!m_userInfo.installDir.empty()) {
                ImGui::Text("Install Directory: %s", m_userInfo.installDir.c_str());
            }
        }

        if (ImGui::CollapsingHeader("DRM & License Verification", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("License Subscribed: %s", m_userInfo.isSubscribed ? "YES (Valid License)" : "NO / Unverified");
            ImGui::Text("VAC Ban Status: %s", m_userInfo.isVACBanned ? "BANNED" : "CLEAN (VAC Secured)");
            ImGui::Text("Cybercafe License: %s", m_userInfo.isCybercafe ? "YES" : "NO");
            ImGui::Text("Low Violence / Censored: %s", m_userInfo.isLowViolence ? "YES" : "NO");
            ImGui::Text("DLC Count: %d", m_userInfo.dlcCount);

            if (ImGui::Button("Re-Verify DRM License")) {
                RefreshUserInfo();
            }
        }

        if (ImGui::CollapsingHeader("Overlay & Rich Presence", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Steam Overlay Supported: %s", m_userInfo.isOverlayEnabled ? "YES" : "NO");
            if (ImGui::Button("Open Steam Community Overlay")) {
                OpenOverlay("Community");
            }
            ImGui::SameLine();
            if (ImGui::Button("Open Friends Overlay")) {
                OpenOverlay("Friends");
            }

            ImGui::Spacing();
            static char richStatus[128] = "DirectX 12 Simulation Studio";
            ImGui::InputText("Rich Presence Status", richStatus, sizeof(richStatus));
            if (ImGui::Button("Broadcast Rich Presence")) {
                SetRichPresence("status", richStatus);
            }
        }

        if (ImGui::CollapsingHeader("Achievements & Stats Tester")) {
            static char achName[64] = "ACH_FIRST_SIMULATION";
            ImGui::InputText("Achievement ID", achName, sizeof(achName));
            if (ImGui::Button("Unlock Achievement (Test)")) {
                UnlockAchievement(achName);
            }
        }
    }
    ImGui::End();
#endif
#endif
}

} // namespace EngineSteam
