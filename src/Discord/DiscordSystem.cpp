#include "DiscordSystem.h"
#if defined(_WIN32)
#define DISCORDPP_IMPLEMENTATION
#include <discordpp.h>
#include <imgui.h>
#include <iostream>
#include <chrono>

namespace EngineDiscord {

struct DiscordSystem::Impl {
    std::unique_ptr<discordpp::Client> client;
};

DiscordSystem& DiscordSystem::Get() {
    static DiscordSystem instance;
    return instance;
}

DiscordSystem::DiscordSystem() {
    auto now = std::chrono::system_clock::now();
    m_startTimestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()
    );
}

DiscordSystem::~DiscordSystem() {
    Shutdown();
}

bool DiscordSystem::Initialize(uint64_t appId, const std::string& publicKey) {
    if (m_initialized) {
        return true;
    }

    if (!appId) { std::cerr << "[Discord] Application ID is required.\n"; return false; }
    m_appId = appId;
    if (!publicKey.empty()) {
        m_publicKey = publicKey;
    }

    std::cout << "[Discord] Initializing Discord Social SDK...\n";
    std::cout << "[Discord] Application ID: " << m_appId << "\n";
    std::cout << "[Discord] Public Key: " << m_publicKey << "\n";

    try {
        m_impl = std::make_unique<Impl>();
        m_impl->client = std::make_unique<discordpp::Client>();

        if (!m_impl->client || !(*m_impl->client)) {
            std::cerr << "[Discord] Failed to create Discord Client instance.\n";
            m_impl.reset();
            return false;
        }

        m_impl->client->SetApplicationId(m_appId);

        // Status Callback
        m_impl->client->SetStatusChangedCallback([this](discordpp::Client::Status status, discordpp::Client::Error error, int32_t errorDetail) {
            switch (status) {
                case discordpp::Client::Status::Disconnected:
                    m_userInfo.statusString = "Disconnected";
                    m_userInfo.isConnected = false;
                    m_userInfo.isReady = false;
                    break;
                case discordpp::Client::Status::Connecting:
                    m_userInfo.statusString = "Connecting";
                    m_userInfo.isConnected = false;
                    m_userInfo.isReady = false;
                    break;
                case discordpp::Client::Status::Connected:
                    m_userInfo.statusString = "Connected";
                    m_userInfo.isConnected = true;
                    m_userInfo.isReady = false;
                    break;
                case discordpp::Client::Status::Ready:
                    m_userInfo.statusString = "Ready";
                    m_userInfo.isConnected = true;
                    m_userInfo.isReady = true;
                    m_needsPresenceUpdate = true;

                    // Fetch user info when ready
                    if (m_impl && m_impl->client) {
                        auto userOpt = m_impl->client->GetCurrentUserV2();
                        if (userOpt.has_value()) {
                            auto& user = *userOpt;
                            m_userInfo.userId = user.Id();
                            m_userInfo.username = user.Username();
                            m_userInfo.displayName = user.DisplayName();
                            auto gName = user.GlobalName();
                            if (gName.has_value()) m_userInfo.globalName = *gName;
                            auto avatar = user.Avatar();
                            if (avatar.has_value()) m_userInfo.avatarHash = *avatar;
                        }
                    }
                    break;
            }
            std::cout << "[Discord] Status changed: " << m_userInfo.statusString 
                      << " (Error: " << (int)error << ", Detail: " << errorDetail << ")\n";
        });

        // User profile update callback
        m_impl->client->SetUserUpdatedCallback([this](uint64_t userId) {
            if (m_impl && m_impl->client) {
                auto userOpt = m_impl->client->GetCurrentUserV2();
                if (userOpt.has_value()) {
                    auto& user = *userOpt;
                    m_userInfo.userId = user.Id();
                    m_userInfo.username = user.Username();
                    m_userInfo.displayName = user.DisplayName();
                    auto gName = user.GlobalName();
                    if (gName.has_value()) m_userInfo.globalName = *gName;
                    auto avatar = user.Avatar();
                    if (avatar.has_value()) m_userInfo.avatarHash = *avatar;
                }
            }
        });

        // Request connected user from local Discord App
        m_impl->client->GetDiscordClientConnectedUser(m_appId, [this](discordpp::ClientResult result, std::optional<discordpp::UserHandle> user) {
            if (result.Successful() && user.has_value()) {
                m_userInfo.isConnected = true;
                m_userInfo.isReady = true;
                m_userInfo.statusString = "Connected to Discord Client";
                m_userInfo.userId = user->Id();
                m_userInfo.username = user->Username();
                m_userInfo.displayName = user->DisplayName();
                auto gName = user->GlobalName();
                if (gName.has_value()) m_userInfo.globalName = *gName;
                auto avatar = user->Avatar();
                if (avatar.has_value()) m_userInfo.avatarHash = *avatar;
                std::cout << "[Discord] Local Discord User detected: " << m_userInfo.username << " (" << m_userInfo.displayName << ") [ID: " << m_userInfo.userId << "]\n";
                UpdatePresence();
            } else {
                std::cout << "[Discord] Local Discord user query note: " << result.ToString() << "\n";
            }
        });

        // Connect to local Discord Client IPC
        m_impl->client->Connect();
        m_initialized = true;
        m_needsPresenceUpdate = true;
        
        // Initial presence push
        UpdatePresence();

        std::cout << "[Discord] Discord Social SDK Initialized successfully.\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Discord] Exception during initialization: " << e.what() << "\n";
        m_impl.reset();
        return false;
    } catch (...) {
        std::cerr << "[Discord] Unknown error during initialization.\n";
        m_impl.reset();
        return false;
    }
}

void DiscordSystem::Shutdown() {
    if (!m_initialized) return;

    std::cout << "[Discord] Shutting down Discord Social SDK...\n";

    if (m_impl && m_impl->client) {
        try {
            m_impl->client->Disconnect();
            m_impl->client.reset();
        } catch (...) {}
    }
    m_impl.reset();

    m_initialized = false;
    m_userInfo.isConnected = false;
    m_userInfo.isReady = false;
    m_userInfo.statusString = "Disconnected";
}

void DiscordSystem::Update(float dt) {
    if (!m_initialized) return;

    // Process all pending Discord callbacks
    try {
        discordpp::RunCallbacks();
    } catch (...) {}

    m_presenceUpdateTimer += dt;
    if (m_needsPresenceUpdate || m_presenceUpdateTimer >= 15.0f) {
        UpdatePresence();
        m_needsPresenceUpdate = false;
        m_presenceUpdateTimer = 0.0f;
    }
}

void DiscordSystem::SetActivity(const std::string& state, const std::string& details) {
    m_presence.state = state;
    m_presence.details = details;
    m_needsPresenceUpdate = true;
}

void DiscordSystem::SetParty(int currentSize, int maxSize, const std::string& partyId, const std::string& joinSecret) {
    m_presence.hasParty = true;
    m_presence.partySize = currentSize;
    m_presence.partyMax = maxSize;
    m_presence.partyId = partyId;
    m_presence.joinSecret = joinSecret;
    m_needsPresenceUpdate = true;
}

void DiscordSystem::ClearParty() {
    m_presence.hasParty = false;
    m_presence.partyId.clear();
    m_presence.joinSecret.clear();
    m_needsPresenceUpdate = true;
}

void DiscordSystem::UpdatePresence() {
    if (!m_initialized || !m_impl || !m_impl->client) return;

    try {
        discordpp::Activity activity;
        activity.SetType(discordpp::ActivityTypes::Playing);
        activity.SetName("Idontknow");

        if (!m_presence.state.empty()) {
            activity.SetState(m_presence.state);
        }
        if (!m_presence.details.empty()) {
            activity.SetDetails(m_presence.details);
        }

        if (m_presence.hasTimestamps && m_startTimestamp > 0) {
            discordpp::ActivityTimestamps timestamps;
            timestamps.SetStart(m_startTimestamp);
            activity.SetTimestamps(timestamps);
        }

        discordpp::ActivityAssets assets;
        if (!m_presence.largeImage.empty()) {
            assets.SetLargeImage(m_presence.largeImage);
        }
        if (!m_presence.largeText.empty()) {
            assets.SetLargeText(m_presence.largeText);
        }
        if (!m_presence.smallImage.empty()) {
            assets.SetSmallImage(m_presence.smallImage);
        }
        if (!m_presence.smallText.empty()) {
            assets.SetSmallText(m_presence.smallText);
        }
        activity.SetAssets(assets);

        if (m_presence.hasParty && m_presence.partyMax > 0) {
            discordpp::ActivityParty party;
            party.SetId(m_presence.partyId.empty() ? "lobby_default" : m_presence.partyId);
            party.SetCurrentSize(m_presence.partySize);
            party.SetMaxSize(m_presence.partyMax);
            party.SetPrivacy(discordpp::ActivityPartyPrivacy::Public);
            activity.SetParty(party);

            if (!m_presence.joinSecret.empty()) {
                discordpp::ActivitySecrets secrets;
                secrets.SetJoin(m_presence.joinSecret);
                activity.SetSecrets(secrets);
            }
        }

        m_impl->client->UpdateRichPresence(std::move(activity), [](discordpp::ClientResult result) {
            if (result.Successful()) {
                std::cout << "[Discord] Rich Presence updated successfully.\n";
            } else {
                std::cout << "[Discord] Rich Presence response: " << result.ToString() << "\n";
            }
        });
    } catch (const std::exception& e) {
        std::cerr << "[Discord] Exception during Rich Presence update: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[Discord] Unknown error during Rich Presence update.\n";
    }
}

void DiscordSystem::DrawUI() {
#if defined(ENGINE_EDITOR)
    if (!m_showUI) return;

    ImGui::SetNextWindowSize(ImVec2(480, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Discord Social Studio & Rich Presence", &m_showUI, ImGuiWindowFlags_None)) {
        ImGui::TextColored(ImVec4(0.45f, 0.55f, 0.95f, 1.0f), "=== DISCORD SOCIAL SDK INTEGRATION ===");
        ImGui::Separator();

        // SDK Credentials
        ImGui::Text("Application ID: %llu", m_appId);
        ImGui::TextWrapped("Public Key: %s", m_publicKey.c_str());
        ImGui::Separator();

        // Status Badge
        ImGui::Text("Connection Status: ");
        ImGui::SameLine();
        if (m_userInfo.isReady) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "[READY / ACTIVE]");
        } else if (m_userInfo.isConnected) {
            ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "[CONNECTED]");
        } else if (m_initialized) {
            ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.2f, 1.0f), "[%s]", m_userInfo.statusString.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "[OFFLINE / UNINITIALIZED]");
        }

        // Current User Profile
        if (ImGui::CollapsingHeader("Discord User Profile", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (m_userInfo.userId != 0) {
                ImGui::Text("User ID: %llu", m_userInfo.userId);
                ImGui::Text("Username: %s", m_userInfo.username.c_str());
                ImGui::Text("Display Name: %s", m_userInfo.displayName.c_str());
                if (!m_userInfo.globalName.empty()) {
                    ImGui::Text("Global Name: %s", m_userInfo.globalName.c_str());
                }
            } else {
                ImGui::TextDisabled("No Discord user profile received yet (Waiting for local Discord app)");
            }
        }

        // Rich Presence Customizer
        if (ImGui::CollapsingHeader("Rich Presence Studio", ImGuiTreeNodeFlags_DefaultOpen)) {
            char stateBuf[128];
            char detailsBuf[128];
            char largeImgBuf[64];
            char largeTxtBuf[128];
            char smallImgBuf[64];
            char smallTxtBuf[128];

            strncpy_s(stateBuf, m_presence.state.c_str(), sizeof(stateBuf) - 1);
            strncpy_s(detailsBuf, m_presence.details.c_str(), sizeof(detailsBuf) - 1);
            strncpy_s(largeImgBuf, m_presence.largeImage.c_str(), sizeof(largeImgBuf) - 1);
            strncpy_s(largeTxtBuf, m_presence.largeText.c_str(), sizeof(largeTxtBuf) - 1);
            strncpy_s(smallImgBuf, m_presence.smallImage.c_str(), sizeof(smallImgBuf) - 1);
            strncpy_s(smallTxtBuf, m_presence.smallText.c_str(), sizeof(smallTxtBuf) - 1);

            if (ImGui::InputText("State", stateBuf, sizeof(stateBuf))) {
                m_presence.state = stateBuf;
            }
            if (ImGui::InputText("Details", detailsBuf, sizeof(detailsBuf))) {
                m_presence.details = detailsBuf;
            }
            if (ImGui::InputText("Large Image Key", largeImgBuf, sizeof(largeImgBuf))) {
                m_presence.largeImage = largeImgBuf;
            }
            if (ImGui::InputText("Large Text", largeTxtBuf, sizeof(largeTxtBuf))) {
                m_presence.largeText = largeTxtBuf;
            }
            if (ImGui::InputText("Small Image Key", smallImgBuf, sizeof(smallImgBuf))) {
                m_presence.smallImage = smallImgBuf;
            }
            if (ImGui::InputText("Small Text", smallTxtBuf, sizeof(smallTxtBuf))) {
                m_presence.smallText = smallTxtBuf;
            }

            ImGui::Checkbox("Multiplayer Party Info", &m_presence.hasParty);
            if (m_presence.hasParty) {
                ImGui::SliderInt("Party Current", &m_presence.partySize, 1, 16);
                ImGui::SliderInt("Party Max", &m_presence.partyMax, 1, 16);
            }

            ImGui::Spacing();
            if (ImGui::Button("Push Update to Discord", ImVec2(200, 30))) {
                UpdatePresence();
            }
        }

        // Actions & Diagnostics
        ImGui::Separator();
        if (ImGui::Button("Reconnect SDK")) {
            if (m_impl && m_impl->client) {
                m_impl->client->Connect();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Presence")) {
            m_presence.state.clear();
            m_presence.details.clear();
            ClearParty();
            UpdatePresence();
        }
    }
    ImGui::End();
#endif
}

} // namespace EngineDiscord
#else

namespace EngineDiscord {

struct DiscordSystem::Impl {};

DiscordSystem& DiscordSystem::Get() {
    static DiscordSystem instance;
    return instance;
}

DiscordSystem::DiscordSystem() = default;
DiscordSystem::~DiscordSystem() { Shutdown(); }

bool DiscordSystem::Initialize(uint64_t appId, const std::string& publicKey) {
    m_appId = appId;
    if (!publicKey.empty()) m_publicKey = publicKey;
    m_initialized = true;
    m_userInfo.isConnected = false;
    m_userInfo.statusString = "Standalone (macOS)";
    return true;
}

void DiscordSystem::Shutdown() {
    m_initialized = false;
}

void DiscordSystem::Update(float) {}
void DiscordSystem::SetActivity(const std::string&, const std::string&) {}
void DiscordSystem::SetParty(int, int, const std::string&, const std::string&) {}
void DiscordSystem::ClearParty() {}
void DiscordSystem::UpdatePresence() {}
void DiscordSystem::DrawUI() {}

} // namespace EngineDiscord
#endif
