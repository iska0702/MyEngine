#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <future>
#include "Asset/ModelThumbnail.h"
class Texture;
#include <DirectXMath.h>

class Renderer;
class Camera;

namespace EngineUI {

enum class AssetType {
    Folder = 0,
    Model3D,       // .fbx, .obj, .gltf, .glb
    Texture,       // .png, .jpg, .jpeg, .dds, .tga, .hdr
    Shader,        // .hlsl, .cso
    Animation,     // .ozz, .bvh, .anim
    Audio,         // .wav, .mp3, .ogg
    Video,         // .mp4, .mkv, .avi, .mov, .webm, .av1.mp4
    ConfigData,    // .json, .ini, .xml, .txt
    SaveGame,      // .save
    Level,         // .level, .map
    Unknown
};

struct AssetEntry {
    std::string name;
    std::string filename;
    std::string extension;
    std::filesystem::path fullPath;
    std::string relativePath; // Relative to src/Content
    AssetType type = AssetType::Unknown;
    uintmax_t fileSize = 0;
    bool isDirectory = false;
    std::string formattedSize;
};

class ContentBrowser {
public:
    static ContentBrowser& Get();

    void Initialize(Renderer* renderer);
    void DrawUI(Renderer* renderer, Camera* camera);

    // Asset Operations
    bool ImportModelFileDialog(Renderer* renderer, Camera* camera);
    bool ImportVideoFileDialog();
    bool TranscodeVideoToAV1(const std::filesystem::path& inputPath, const std::filesystem::path& targetDir);
    bool ImportGenericFileDialog();

    void SetRootPath(const std::filesystem::path& path);
    const std::filesystem::path& GetRootPath() const { return m_rootContentPath; }
    const std::filesystem::path& GetCurrentPath() const { return m_currentDirectory; }

    void Refresh();
    void SetOpen(bool open) { m_isOpen = open; }
    bool IsOpen() const { return m_isOpen; }
    void ToggleOpen() { m_isOpen = !m_isOpen; }

private:
    ContentBrowser();
    ~ContentBrowser();

    void ScanDirectory(const std::filesystem::path& dir);
    void DrawDirectoryTree(const std::filesystem::path& dir);
    void DrawAssetGrid(Renderer* renderer, Camera* camera);
    void DrawBreadcrumbs();
    void DrawToolbar(Renderer* renderer, Camera* camera);
    void DrawTranscodingModal();

    AssetType DetectAssetType(const std::filesystem::path& path) const;
    std::string FormatBytes(uintmax_t bytes) const;
    const char* GetAssetIcon(AssetType type) const;

    bool m_isOpen = false;
    bool m_initialized = false;

    std::filesystem::path m_rootContentPath;
    std::filesystem::path m_currentDirectory;
    std::filesystem::path m_selectedAssetPath;

    std::vector<AssetEntry> m_currentItems;
    std::unordered_map<std::string,std::shared_ptr<Texture>> m_thumbnails;
    std::unordered_map<std::string,EngineAsset::ThumbnailLines> m_modelThumbnails;
    std::future<EngineAsset::ThumbnailLines> m_thumbnailTask;
    std::string m_thumbnailTaskPath;
    std::vector<std::filesystem::path> m_history;
    int m_historyIndex = -1;

    char m_searchFilter[128] = "";
    int m_categoryFilter = 0; // 0: All, 1: Models, 2: Textures, 3: Shaders, 4: Animations, 5: Videos, 6: Data
    float m_thumbnailSize = 84.0f;
    float m_padding = 12.0f;

    // Status notification
    std::string m_statusMessage;
    float m_statusTimer = 0.0f;
    bool m_statusIsError = false;

    // Video Transcoding State
    bool m_isTranscoding = false;
    bool m_showTranscodingModal = false;
    std::string m_transcodeLogs;
    std::string m_transcodingFileName;
    bool m_transcodeSuccess = false;
};

} // namespace EngineUI
