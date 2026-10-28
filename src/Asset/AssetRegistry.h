#pragma once

#include "AssetTypes.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace EngineAsset {

class AssetRegistry {
public:
    static AssetRegistry& Get();

    bool Initialize(const std::string& contentRoot = "");
    void Shutdown();

    // Scanning & Indexing
    void ScanDirectory(const std::string& rootDir);
    void RegisterAsset(const AssetMetadata& meta);
    void UnregisterAsset(const std::string& guidOrPath);

    // Queries
    const AssetMetadata* FindAssetByGUID(const std::string& guid) const;
    const AssetMetadata* FindAssetByPath(const std::string& virtualPath) const;
    std::vector<const AssetMetadata*> GetAssetsByType(AssetType type) const;
    const std::unordered_map<std::string, AssetMetadata>& GetAllAssets() const { return m_assetsByGUID; }
    size_t GetAssetCount() const { return m_assetsByGUID.size(); }
    const std::string& GetContentRoot() const { return m_contentRoot; }

    // GUID Generation
    static std::string GenerateGUID(const std::string& seed = "");

    // Persistence
    bool SaveManifest(const std::string& filepath = "");
    bool LoadManifest(const std::string& filepath = "");

    // UI
    void DrawUI();
    void ToggleUI() { m_uiOpen = !m_uiOpen; }
    void SetUIOpen(bool open) { m_uiOpen = open; }
    bool IsUIOpen() const { return m_uiOpen; }

private:
    AssetRegistry() = default;
    ~AssetRegistry() = default;
    AssetRegistry(const AssetRegistry&) = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;

    std::string m_contentRoot = "src/Content";
    std::unordered_map<std::string, AssetMetadata> m_assetsByGUID;
    std::unordered_map<std::string, std::string> m_pathToGUID;
    mutable std::recursive_mutex m_mutex;

    bool m_initialized = false;
    bool m_uiOpen = false;

    // UI filtering state
    char m_searchFilter[128] = { 0 };
    int m_selectedTypeFilter = -1; // -1 = All
    std::string m_selectedGUID = "";
};

} // namespace EngineAsset
