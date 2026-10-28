#include "AssetRegistry.h"
#include "Platform/PlatformUtils.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <random>
#include <chrono>

#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
#include <imgui.h>
#endif

namespace EngineAsset {

AssetRegistry& AssetRegistry::Get() {
    static AssetRegistry instance;
    return instance;
}

bool AssetRegistry::Initialize(const std::string& contentRoot) {
    std::string root = contentRoot.empty() ? EnginePlatform::PlatformUtils::GetContentRoot() : contentRoot;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_initialized) return true;
        m_contentRoot = root;
        m_initialized = true;
    }

    std::cout << "[AssetRegistry] Initializing content root: " << m_contentRoot << std::endl;

#if !defined(ENGINE_PLATFORM_PS4) && !defined(__PS4__) && !defined(__ORBIS__)
    // Scan Content Directory (desktop/development builds only)
    ScanDirectory(m_contentRoot);
#endif

    std::cout << "[AssetRegistry] Indexed " << m_assetsByGUID.size() << " assets." << std::endl;
    return true;
}

void AssetRegistry::Shutdown() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_assetsByGUID.clear();
    m_pathToGUID.clear();
    m_initialized = false;
}

std::string AssetRegistry::GenerateGUID(const std::string& seed) {
    if (!seed.empty()) {
        // Deterministic hash based on seed string
        uint64_t h1 = 14695981039346656037ULL;
        uint64_t h2 = 1099511628211ULL;
        for (char c : seed) {
            h1 = (h1 ^ static_cast<uint64_t>(c)) * 1099511628211ULL;
            h2 = (h2 ^ static_cast<uint64_t>(c * 31)) * 14695981039346656037ULL;
        }
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        ss << std::setw(8) << static_cast<uint32_t>(h1 >> 32) << "-";
        ss << std::setw(4) << static_cast<uint16_t>(h1 >> 16) << "-";
        ss << std::setw(4) << static_cast<uint16_t>(h1) << "-";
        ss << std::setw(4) << static_cast<uint16_t>(h2 >> 48) << "-";
        ss << std::setw(12) << (h2 & 0xFFFFFFFFFFFFULL);
        return ss.str();
    }

    // Cryptographic-like random GUID
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    uint64_t p1 = dis(gen);
    uint64_t p2 = dis(gen);

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << static_cast<uint32_t>(p1 >> 32) << "-";
    ss << std::setw(4) << static_cast<uint16_t>(p1 >> 16) << "-";
    ss << std::setw(4) << ((static_cast<uint16_t>(p1) & 0x0FFF) | 0x4000) << "-"; // UUID v4
    ss << std::setw(4) << ((static_cast<uint16_t>(p2 >> 48) & 0x3FFF) | 0x8000) << "-";
    ss << std::setw(12) << (p2 & 0xFFFFFFFFFFFFULL);
    return ss.str();
}

void AssetRegistry::ScanDirectory(const std::string& rootDir) {
#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    // On PS4 Orbis OS, assets are bundled in PAK archives or accessed directly via VFS.
    return;
#else
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(rootDir, ec) || !fs::is_directory(rootDir, ec)) {
        return;
    }

    std::string normRoot = fs::weakly_canonical(rootDir, ec).generic_string();

    try {
        for (const auto& entry : fs::recursive_directory_iterator(rootDir, fs::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;

            fs::path filePath = entry.path();
            std::string ext = filePath.extension().generic_string();
            AssetType type = GetAssetTypeFromExtension(ext);
            if (type == AssetType::Unknown) continue;

            std::string diskPath = filePath.generic_string();
            std::string relPath = fs::relative(filePath, rootDir, ec).generic_string();

            AssetMetadata meta;
            meta.virtualPath = relPath;
            meta.diskPath = diskPath;
            meta.name = filePath.filename().generic_string();
            meta.type = type;
            meta.fileSize = entry.file_size(ec);
            meta.guid = GenerateGUID(relPath);

            auto ftime = entry.last_write_time(ec);
            meta.lastModifiedTime = std::chrono::duration_cast<std::chrono::seconds>(
                ftime.time_since_epoch()).count();

            RegisterAsset(meta);
        }
    } catch (const std::exception& ex) {
        std::cerr << "[AssetRegistry] Scan error: " << ex.what() << std::endl;
    }
#endif
}

void AssetRegistry::RegisterAsset(const AssetMetadata& meta) {
    if (meta.guid.empty() || meta.virtualPath.empty()) return;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_assetsByGUID[meta.guid] = meta;
    m_pathToGUID[meta.virtualPath] = meta.guid;
}

void AssetRegistry::UnregisterAsset(const std::string& guidOrPath) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto itGUID = m_assetsByGUID.find(guidOrPath);
    if (itGUID != m_assetsByGUID.end()) {
        m_pathToGUID.erase(itGUID->second.virtualPath);
        m_assetsByGUID.erase(itGUID);
        return;
    }

    auto itPath = m_pathToGUID.find(guidOrPath);
    if (itPath != m_pathToGUID.end()) {
        m_assetsByGUID.erase(itPath->second);
        m_pathToGUID.erase(itPath);
    }
}

const AssetMetadata* AssetRegistry::FindAssetByGUID(const std::string& guid) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_assetsByGUID.find(guid);
    if (it != m_assetsByGUID.end()) return &it->second;
    return nullptr;
}

const AssetMetadata* AssetRegistry::FindAssetByPath(const std::string& virtualPath) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_pathToGUID.find(virtualPath);
    if (it != m_pathToGUID.end()) {
        auto itAsset = m_assetsByGUID.find(it->second);
        if (itAsset != m_assetsByGUID.end()) return &itAsset->second;
    }
    return nullptr;
}

std::vector<const AssetMetadata*> AssetRegistry::GetAssetsByType(AssetType type) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<const AssetMetadata*> results;
    for (const auto& pair : m_assetsByGUID) {
        if (pair.second.type == type) {
            results.push_back(&pair.second);
        }
    }
    return results;
}

bool AssetRegistry::SaveManifest(const std::string& filepath) {
    std::string target = filepath.empty() ? EnginePlatform::PlatformUtils::ResolveContentPath("asset_manifest.json") : filepath;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::ofstream file(target);
    if (!file.is_open()) return false;

    file << "{\n  \"assets\": [\n";
    size_t count = 0;
    for (const auto& pair : m_assetsByGUID) {
        const auto& a = pair.second;
        file << "    {\n";
        file << "      \"guid\": \"" << a.guid << "\",\n";
        file << "      \"path\": \"" << a.virtualPath << "\",\n";
        file << "      \"name\": \"" << a.name << "\",\n";
        file << "      \"type\": \"" << AssetTypeToString(a.type) << "\",\n";
        file << "      \"size\": " << a.fileSize << "\n";
        file << "    }" << (++count < m_assetsByGUID.size() ? "," : "") << "\n";
    }
    file << "  ]\n}\n";
    return true;
}

bool AssetRegistry::LoadManifest(const std::string& filepath) {
    std::string target = filepath.empty() ? EnginePlatform::PlatformUtils::ResolveContentPath("asset_manifest.json") : filepath;
    std::ifstream file(target);
    return file.is_open();
}

void AssetRegistry::DrawUI() {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    if (!m_uiOpen) return;

    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(" Asset Registry & Catalogue", &m_uiOpen)) {
        ImGui::End();
        return;
    }

    // Top Controls
    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Total Assets Indexed: %zu", m_assetsByGUID.size());
    ImGui::SameLine();
    if (ImGui::Button(" Rescan Content")) {
        ScanDirectory(m_contentRoot);
    }
    ImGui::SameLine();
    if (ImGui::Button(" Save Manifest")) {
        SaveManifest();
    }

    ImGui::Separator();

    // Filters
    ImGui::SetNextItemWidth(250);
    ImGui::InputTextWithHint("##SearchAsset", "Search assets...", m_searchFilter, sizeof(m_searchFilter));
    ImGui::SameLine();

    const char* typeFilterNames[] = {
        "All Types", "Model", "Texture", "Audio", "Script", "Level", "Prefab", "Material", "Shader", "Animation"
    };
    int curFilterIdx = m_selectedTypeFilter + 1; // -1 -> 0
    ImGui::SetNextItemWidth(150);
    if (ImGui::Combo("Filter Type", &curFilterIdx, typeFilterNames, IM_ARRAYSIZE(typeFilterNames))) {
        m_selectedTypeFilter = curFilterIdx - 1;
    }

    ImGui::Separator();

    // Splitter: Table on left, Details on right
    float tableWidth = ImGui::GetContentRegionAvail().x * 0.65f;

    ImGui::BeginChild("AssetTablePane", ImVec2(tableWidth, 0), true);
    if (ImGui::BeginTable("AssetRegistryTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Virtual Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        std::string searchStr = m_searchFilter;
        std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

        for (const auto& pair : m_assetsByGUID) {
            const auto& meta = pair.second;

            // Type filter
            if (m_selectedTypeFilter >= 0 && static_cast<int>(meta.type) != (m_selectedTypeFilter + 1)) {
                continue;
            }

            // Search filter
            if (!searchStr.empty()) {
                std::string lowerName = meta.name;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                std::string lowerPath = meta.virtualPath;
                std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
                if (lowerName.find(searchStr) == std::string::npos && lowerPath.find(searchStr) == std::string::npos) {
                    continue;
                }
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", AssetTypeToIcon(meta.type));

            ImGui::TableNextColumn();
            bool isSelected = (m_selectedGUID == meta.guid);
            if (ImGui::Selectable(meta.name.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                m_selectedGUID = meta.guid;
            }

            ImGui::TableNextColumn();
            if (meta.fileSize < 1024) {
                ImGui::Text("%llu B", meta.fileSize);
            } else if (meta.fileSize < 1024 * 1024) {
                ImGui::Text("%.1f KB", meta.fileSize / 1024.0f);
            } else {
                ImGui::Text("%.2f MB", meta.fileSize / (1024.0f * 1024.0f));
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(meta.virtualPath.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Details Panel
    ImGui::BeginChild("AssetDetailPane", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Asset Inspector");
    ImGui::Separator();

    const AssetMetadata* selectedAsset = FindAssetByGUID(m_selectedGUID);
    if (selectedAsset) {
        ImGui::Text("Name: %s", selectedAsset->name.c_str());
        ImGui::Text("Type: %s (%s)", AssetTypeToString(selectedAsset->type), AssetTypeToIcon(selectedAsset->type));
        ImGui::Text("Virtual: %s", selectedAsset->virtualPath.c_str());
        ImGui::TextWrapped("Disk: %s", selectedAsset->diskPath.c_str());
        ImGui::Text("Size: %llu bytes", selectedAsset->fileSize);
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "GUID: %s", selectedAsset->guid.c_str());
        if (ImGui::Button(" Copy GUID")) {
            ImGui::SetClipboardText(selectedAsset->guid.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button(" Copy Path")) {
            ImGui::SetClipboardText(selectedAsset->virtualPath.c_str());
        }
    } else {
        ImGui::TextDisabled("Select an asset from the table to view metadata & GUID.");
    }
    ImGui::EndChild();

    ImGui::End();
#endif
}

} // namespace EngineAsset
