#include "ContentBrowser.h"
#include "Core/Project.h"
#include "Renderer/Renderer.h"
#include "Renderer/Camera.h"
#include "Level/LevelSystem.h"
#include "Renderer/Texture.h"
#include "Prefab/PrefabSystem.h"
#include <imgui.h>
#if defined(_WIN32)
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#else
#include "Platform/PlatformTypes.h"
#include "Platform/PlatformUtils.h"
#endif
#include <algorithm>
#include <fstream>
#include <sstream>
#include <thread>
#include <future>
#include <chrono>

namespace EngineUI {

ContentBrowser& ContentBrowser::Get() {
    static ContentBrowser s_instance;
    return s_instance;
}

ContentBrowser::ContentBrowser() {
}

ContentBrowser::~ContentBrowser() {
}

void ContentBrowser::Initialize(Renderer* renderer) {
#ifndef ENGINE_RELEASE
    if (m_initialized) return;

    if (!EngineProject::active) {
        LOG("ContentBrowser: no project is open");
        return;
    }
    m_rootContentPath = EngineProject::current.Resolve(EngineProject::current.content);

    // Ensure standard asset subfolders exist
    std::error_code ec;
    std::filesystem::create_directories(m_rootContentPath / "Models", ec);
    std::filesystem::create_directories(m_rootContentPath / "Textures", ec);
    std::filesystem::create_directories(m_rootContentPath / "Videos", ec);
    std::filesystem::create_directories(m_rootContentPath / "Shaders", ec);
    std::filesystem::create_directories(m_rootContentPath / "Animations", ec);
    std::filesystem::create_directories(m_rootContentPath / "Config", ec);
    std::filesystem::create_directories(m_rootContentPath / "Levels", ec);

    m_currentDirectory = m_rootContentPath;
    m_history.push_back(m_currentDirectory);
    m_historyIndex = 0;

    ScanDirectory(m_currentDirectory);
    m_initialized = true;
    LOG("ContentBrowser: Initialized at root " << m_rootContentPath.string());
#endif
}

void ContentBrowser::SetRootPath(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        m_rootContentPath = std::filesystem::canonical(path, ec);
        m_currentDirectory = m_rootContentPath;
        ScanDirectory(m_currentDirectory);
    }
}

void ContentBrowser::Refresh() {
    ScanDirectory(m_currentDirectory);
}

AssetType ContentBrowser::DetectAssetType(const std::filesystem::path& path) const {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) return AssetType::Folder;

    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".dae") return AssetType::Model3D;
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds" || ext == ".tga" || ext == ".hdr" || ext == ".bmp") return AssetType::Texture;
    if (ext == ".mp4" || ext == ".mkv" || ext == ".avi" || ext == ".mov" || ext == ".webm" || ext == ".ivf") return AssetType::Video;
    if (ext == ".hlsl" || ext == ".cso" || ext == ".hlsli") return AssetType::Shader;
    if (ext == ".ozz" || ext == ".bvh" || ext == ".anim") return AssetType::Animation;
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") return AssetType::Audio;
    if (ext == ".save") return AssetType::SaveGame;
    if (ext == ".level" || ext == ".map") return AssetType::Level;
    if (ext == ".json" || ext == ".ini" || ext == ".txt" || ext == ".xml" || ext == ".cfg") return AssetType::ConfigData;

    return AssetType::Unknown;
}

std::string ContentBrowser::FormatBytes(uintmax_t bytes) const {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) {
        char buf[32];
        sprintf_s(buf, "%.1f KB", (double)bytes / 1024.0);
        return std::string(buf);
    }
    char buf[32];
    sprintf_s(buf, "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    return std::string(buf);
}

const char* ContentBrowser::GetAssetIcon(AssetType type) const {
    switch (type) {
        case AssetType::Folder:     return "[DIR]";
        case AssetType::Model3D:    return "[3D]";
        case AssetType::Texture:    return "[TEX]";
        case AssetType::Video:      return "[AV1]";
        case AssetType::Shader:     return "[FX]";
        case AssetType::Animation:  return "[ANIM]";
        case AssetType::Audio:      return "[SND]";
        case AssetType::SaveGame:   return "[SAVE]";
        case AssetType::Level:      return "[MAP]";
        case AssetType::ConfigData: return "[DATA]";
        default:                    return "[FILE]";
    }
}

void ContentBrowser::ScanDirectory(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        m_currentDirectory = m_rootContentPath;
    } else {
        m_currentDirectory = dir;
    }
    m_currentItems.clear();

    std::vector<AssetEntry> folders;
    std::vector<AssetEntry> files;

    for (const auto& entry : std::filesystem::directory_iterator(m_currentDirectory, std::filesystem::directory_options::skip_permission_denied, ec)) {
        AssetEntry item;
        item.fullPath = entry.path();
        item.filename = entry.path().filename().string();
        item.extension = entry.path().extension().string();
        item.name = entry.path().stem().string();
        item.isDirectory = entry.is_directory(ec);
        item.type = DetectAssetType(entry.path());

        // Relative path to root
        try {
            item.relativePath = std::filesystem::relative(entry.path(), m_rootContentPath.parent_path()).string();
            std::replace(item.relativePath.begin(), item.relativePath.end(), '\\', '/');
        } catch (...) {
            item.relativePath = item.filename;
        }

        if (item.isDirectory) {
            item.fileSize = 0;
            item.formattedSize = "Folder";
            folders.push_back(item);
        } else {
            item.fileSize = entry.file_size(ec);
            item.formattedSize = FormatBytes(item.fileSize);
            files.push_back(item);
        }
    }

    // Sort folders and files alphabetically
    std::sort(folders.begin(), folders.end(), [](const AssetEntry& a, const AssetEntry& b) {
        return a.filename < b.filename;
    });
    std::sort(files.begin(), files.end(), [](const AssetEntry& a, const AssetEntry& b) {
        return a.filename < b.filename;
    });

    for (const auto& f : folders) m_currentItems.push_back(f);
    for (const auto& f : files) m_currentItems.push_back(f);
}

bool ContentBrowser::ImportModelFileDialog(Renderer* renderer, Camera* camera) {
    char fileBuffer[4096] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(OPENFILENAMEA);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "3D Models (*.fbx;*.obj;*.gltf;*.glb)\0*.fbx;*.obj;*.gltf;*.glb\0Wavefront OBJ (*.obj)\0*.obj\0Autodesk FBX (*.fbx)\0*.fbx\0glTF / GLB (*.gltf;*.glb)\0*.gltf;*.glb\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = sizeof(fileBuffer);
    ofn.lpstrTitle = "Select 3D Model to Import into project Content";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        std::filesystem::path srcPath(fileBuffer);
        std::error_code ec;

        // Destination: project Content/Models or current subfolder
        std::filesystem::path targetDir = m_currentDirectory;
        if (targetDir == m_rootContentPath) {
            targetDir = m_rootContentPath / "Models";
        }
        std::filesystem::create_directories(targetDir, ec);

        std::filesystem::path destPath = targetDir / srcPath.filename();

        // Copy source model file
        std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::overwrite_existing, ec);

        // Also copy companion textures or .mtl if present in source directory
        std::filesystem::path srcDir = srcPath.parent_path();
        std::string stem = srcPath.stem().string();
        for (const auto& companion : std::filesystem::directory_iterator(srcDir, ec)) {
            if (companion.is_regular_file(ec)) {
                std::string compExt = companion.path().extension().string();
                std::transform(compExt.begin(), compExt.end(), compExt.begin(), ::tolower);
                if (compExt == ".mtl" || compExt == ".png" || compExt == ".jpg" || compExt == ".jpeg" || compExt == ".dds" || compExt == ".tga") {
                    std::filesystem::path compDest = targetDir / companion.path().filename();
                    std::filesystem::copy_file(companion.path(), compDest, std::filesystem::copy_options::overwrite_existing, ec);
                }
            }
        }

        Refresh();

        // Spawn into scene in front of camera
        DirectX::XMFLOAT3 spawnPos = { 0.0f, 1.5f, 0.0f };
        if (camera) {
            DirectX::XMFLOAT3 cPos = camera->GetPosition();
            DirectX::XMFLOAT3 cFwd = camera->GetForwardVector();
            spawnPos = { cPos.x + cFwd.x * 3.0f, (std::max)(cPos.y + cFwd.y * 3.0f, 0.5f), cPos.z + cFwd.z * 3.0f };
        }

        if (renderer) {
            renderer->SpawnDynamicRenderModel(destPath.string().c_str(), spawnPos);
        }

        m_statusMessage = "Imported " + srcPath.filename().string() + " to " + targetDir.filename().string() + " and spawned into scene!";
        m_statusTimer = 5.0f;
        m_statusIsError = false;
        return true;
    }
    return false;
}

bool ContentBrowser::ImportVideoFileDialog() {
    char fileBuffer[4096] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(OPENFILENAMEA);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "Video Files (*.mp4;*.mkv;*.avi;*.mov;*.webm)\0*.mp4;*.mkv;*.avi;*.mov;*.webm\0MP4 Video (*.mp4)\0*.mp4\0Matroska Video (*.mkv)\0*.mkv\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = sizeof(fileBuffer);
    ofn.lpstrTitle = "Select Video to Transcode to AV1 and Import into project Content";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        std::filesystem::path srcPath(fileBuffer);
        std::filesystem::path targetDir = m_currentDirectory;
        if (targetDir == m_rootContentPath) {
            targetDir = m_rootContentPath / "Videos";
        }
        return TranscodeVideoToAV1(srcPath, targetDir);
    }
    return false;
}

bool ContentBrowser::TranscodeVideoToAV1(const std::filesystem::path& inputPath, const std::filesystem::path& targetDir) {
    std::error_code ec;
    std::filesystem::create_directories(targetDir, ec);

    std::string stem = inputPath.stem().string();
    std::filesystem::path outputPath = targetDir / (stem + ".av1.mp4");

    m_isTranscoding = true;
    m_showTranscodingModal = true;
    m_transcodingFileName = inputPath.filename().string();
    m_transcodeLogs = "Starting AV1 Transcoding for: " + inputPath.string() + "\n";
    m_transcodeLogs += "Target Destination: " + outputPath.string() + "\n\n";

    std::thread([this, inputPath, outputPath]() {
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        std::filesystem::path scriptPath = exeDir.parent_path() / "tools" / "Content" / "transcode_to_av1.ps1";

        std::string cmd = "powershell.exe -ExecutionPolicy Bypass -NoProfile -File \"" + scriptPath.string() +
                          "\" -InputPath \"" + inputPath.string() +
                          "\" -OutputPath \"" + outputPath.string() + "\"";

        FILE* pipe = _popen(cmd.c_str(), "r");
        if (!pipe) {
            m_transcodeLogs += "ERROR: Failed to launch AV1 Transcoder process!\n";
            m_isTranscoding = false;
            m_transcodeSuccess = false;
            return;
        }

        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            m_transcodeLogs += buffer;
        }
        int returnCode = _pclose(pipe);

        m_isTranscoding = false;
        m_transcodeSuccess = (returnCode == 0);
        if (m_transcodeSuccess) {
            m_statusMessage = "Successfully transcoded " + inputPath.filename().string() + " to AV1 (" + outputPath.filename().string() + ")!";
            m_statusTimer = 6.0f;
            m_statusIsError = false;
        } else {
            m_statusMessage = "AV1 Transcoding failed. Check logs.";
            m_statusTimer = 6.0f;
            m_statusIsError = true;
        }
        Refresh();
    }).detach();

    return true;
}

bool ContentBrowser::ImportGenericFileDialog() {
    char fileBuffer[4096] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(OPENFILENAMEA);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "All Supported Assets\0*.fbx;*.obj;*.gltf;*.glb;*.png;*.jpg;*.jpeg;*.dds;*.tga;*.hdr;*.mp4;*.mkv;*.hlsl;*.cso;*.ozz;*.bvh;*.json;*.save\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = sizeof(fileBuffer);
    ofn.lpstrTitle = "Select Asset File to Import into project Content";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        std::filesystem::path srcPath(fileBuffer);
        std::error_code ec;

        // If user picked video file, automatically offer/perform AV1 transcode!
        std::string ext = srcPath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".mp4" || ext == ".mkv" || ext == ".avi" || ext == ".mov") {
            std::filesystem::path targetDir = m_currentDirectory;
            if (targetDir == m_rootContentPath) {
                targetDir = m_rootContentPath / "Videos";
            }
            return TranscodeVideoToAV1(srcPath, targetDir);
        }

        std::filesystem::path destPath = m_currentDirectory / srcPath.filename();
        std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::overwrite_existing, ec);

        Refresh();
        m_statusMessage = "Copied " + srcPath.filename().string() + " into project Content!";
        m_statusTimer = 4.0f;
        m_statusIsError = false;
        return true;
    }
    return false;
}

void ContentBrowser::DrawBreadcrumbs() {
    std::vector<std::filesystem::path> parts;
    std::filesystem::path curr = m_currentDirectory;

    while (curr != m_rootContentPath.parent_path() && !curr.empty()) {
        parts.push_back(curr);
        if (curr == m_rootContentPath) break;
        curr = curr.parent_path();
    }
    std::reverse(parts.begin(), parts.end());

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));

    // History Back Button
    bool canBack = (m_historyIndex > 0);
    if (!canBack) ImGui::BeginDisabled();
    if (ImGui::Button("<##Back")) {
        if (m_historyIndex > 0) {
            m_historyIndex--;
            m_currentDirectory = m_history[m_historyIndex];
            ScanDirectory(m_currentDirectory);
        }
    }
    if (!canBack) ImGui::EndDisabled();

    ImGui::SameLine();

    // History Forward Button
    bool canFwd = (m_historyIndex >= 0 && m_historyIndex < (int)m_history.size() - 1);
    if (!canFwd) ImGui::BeginDisabled();
    if (ImGui::Button(">##Fwd")) {
        if (canFwd) {
            m_historyIndex++;
            m_currentDirectory = m_history[m_historyIndex];
            ScanDirectory(m_currentDirectory);
        }
    }
    if (!canFwd) ImGui::EndDisabled();

    ImGui::SameLine();

    // Up directory button
    bool canUp = (m_currentDirectory != m_rootContentPath);
    if (!canUp) ImGui::BeginDisabled();
    if (ImGui::Button("..##Up")) {
        m_currentDirectory = m_currentDirectory.parent_path();
        m_history.push_back(m_currentDirectory);
        m_historyIndex = (int)m_history.size() - 1;
        ScanDirectory(m_currentDirectory);
    }
    if (!canUp) ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    for (size_t i = 0; i < parts.size(); ++i) {
        std::string label = (parts[i] == m_rootContentPath) ? "project Content" : parts[i].filename().string();
        if (ImGui::Button((label + "##crumb" + std::to_string(i)).c_str())) {
            m_currentDirectory = parts[i];
            m_history.push_back(m_currentDirectory);
            m_historyIndex = (int)m_history.size() - 1;
            ScanDirectory(m_currentDirectory);
        }
        if (i < parts.size() - 1) {
            ImGui::SameLine();
            ImGui::TextDisabled(">");
            ImGui::SameLine();
        }
    }

    ImGui::PopStyleVar();
}

void ContentBrowser::DrawToolbar(Renderer* renderer, Camera* camera) {
    // Import 3D Model (Primary Blue action button)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.85f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.55f, 0.95f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.38f, 0.75f, 1.0f));
    if (ImGui::Button("+ Import 3D Model...")) {
        ImportModelFileDialog(renderer, camera);
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();

    // Import Video (MP4/MKV -> AV1) (Purple/Indigo button)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.48f, 0.22f, 0.75f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.32f, 0.88f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.38f, 0.16f, 0.65f, 1.0f));
    if (ImGui::Button(" Import Video (AV1)...")) {
        ImportVideoFileDialog();
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    if (ImGui::Button("+ Import File...")) {
        ImportGenericFileDialog();
    }

    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        Refresh();
    }

    ImGui::SameLine();
    if (ImGui::Button("Open in Explorer")) {
        ShellExecuteA(NULL, "open", m_currentDirectory.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputTextWithHint("##Search", "Search assets...", m_searchFilter, sizeof(m_searchFilter));

    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::SliderFloat("##Zoom", &m_thumbnailSize, 56.0f, 128.0f, "Zoom: %.0f");

    // Category Filter Chips
    const char* categories[] = { "All", "3D Models", "Textures", "Videos (AV1)", "Shaders", "Animations", "Data/Config" };
    ImGui::Spacing();
    for (int i = 0; i < 7; ++i) {
        if (i > 0) ImGui::SameLine();
        bool isSelected = (m_categoryFilter == i);
        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.35f, 1.0f));
        }
        if (ImGui::Button(categories[i])) {
            m_categoryFilter = i;
        }
        if (isSelected) {
            ImGui::PopStyleColor();
        }
    }
}

void ContentBrowser::DrawDirectoryTree(const std::filesystem::path& dir) {
    std::error_code ec;
    std::string folderName = (dir == m_rootContentPath) ? "Content" : dir.filename().string();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (dir == m_currentDirectory) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool hasSubdirs = false;
    for (const auto& entry : std::filesystem::directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (entry.is_directory(ec)) {
            hasSubdirs = true;
            break;
        }
    }

    if (!hasSubdirs) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    bool nodeOpen = ImGui::TreeNodeEx((folderName + "##tree" + dir.string()).c_str(), flags);

    if (ImGui::IsItemClicked()) {
        m_currentDirectory = dir;
        m_history.push_back(m_currentDirectory);
        m_historyIndex = (int)m_history.size() - 1;
        ScanDirectory(m_currentDirectory);
    }

    if (nodeOpen) {
        if (hasSubdirs) {
            for (const auto& entry : std::filesystem::directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (entry.is_directory(ec)) {
                    DrawDirectoryTree(entry.path());
                }
            }
        }
        ImGui::TreePop();
    }
}

void ContentBrowser::DrawAssetGrid(Renderer* renderer, Camera* camera) {
    if(m_thumbnailTask.valid()&&m_thumbnailTask.wait_for(std::chrono::seconds(0))==std::future_status::ready){m_modelThumbnails[m_thumbnailTaskPath]=m_thumbnailTask.get();}
    float panelWidth = ImGui::GetContentRegionAvail().x;
    float cellSize = m_thumbnailSize + m_padding;
    int columnCount = (int)(panelWidth / cellSize);
    if (columnCount < 1) columnCount = 1;

    std::string searchLower = m_searchFilter;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

    ImGui::BeginChild("AssetGridScrollArea", ImVec2(0, -28), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    int itemIdx = 0;
    std::filesystem::path deletePath;
    std::string deleteName;
    for (const auto& item : m_currentItems) {
        // Search Filter
        if (!searchLower.empty()) {
            std::string nameLower = item.filename;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            if (nameLower.find(searchLower) == std::string::npos) continue;
        }

        // Category Filter
        if (m_categoryFilter == 1 && item.type != AssetType::Model3D && !item.isDirectory) continue;
        if (m_categoryFilter == 2 && item.type != AssetType::Texture && !item.isDirectory) continue;
        if (m_categoryFilter == 3 && item.type != AssetType::Video && !item.isDirectory) continue;
        if (m_categoryFilter == 4 && item.type != AssetType::Shader && !item.isDirectory) continue;
        if (m_categoryFilter == 5 && item.type != AssetType::Animation && !item.isDirectory) continue;
        if (m_categoryFilter == 6 && item.type != AssetType::ConfigData && item.type != AssetType::SaveGame && !item.isDirectory) continue;

        if (itemIdx > 0 && (itemIdx % columnCount) != 0) {
            ImGui::SameLine();
        }

        ImGui::PushID(item.filename.c_str());

        // Card button styling based on asset type
        ImVec4 buttonColor = ImVec4(0.16f, 0.17f, 0.20f, 0.8f);
        if (item.isDirectory) {
            buttonColor = ImVec4(0.20f, 0.24f, 0.32f, 0.9f);
        } else if (item.type == AssetType::Model3D) {
            buttonColor = ImVec4(0.18f, 0.28f, 0.38f, 0.9f);
        } else if (item.type == AssetType::Texture) {
            buttonColor = ImVec4(0.28f, 0.20f, 0.34f, 0.9f);
        } else if (item.type == AssetType::Video) {
            buttonColor = ImVec4(0.35f, 0.18f, 0.45f, 0.9f);
        } else if (item.type == AssetType::Level) {
            buttonColor = ImVec4(0.18f, 0.38f, 0.28f, 0.9f);
        } else if (item.type == AssetType::Shader) {
            buttonColor = ImVec4(0.20f, 0.32f, 0.22f, 0.9f);
        }

        bool isSelected = (m_selectedAssetPath == item.fullPath);
        if (isSelected) {
            buttonColor = ImVec4(0.25f, 0.45f, 0.70f, 1.0f);
        }

        ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

        std::string buttonId = "##card_" + item.filename;
        if (ImGui::Button(buttonId.c_str(), ImVec2(m_thumbnailSize, m_thumbnailSize))) {
            m_selectedAssetPath = item.fullPath;
        }

        if(!item.isDirectory&&ImGui::BeginDragDropSource()){
            std::string path=item.fullPath.generic_string();ImGui::SetDragDropPayload("IDK_ASSET",path.c_str(),path.size()+1);ImGui::TextUnformatted(item.filename.c_str());ImGui::EndDragDropSource();
        }
        if(item.type==AssetType::Texture&&renderer){
            auto path=item.fullPath.generic_string();auto it=m_thumbnails.find(path);
            // Bound the cache; texture decoding remains asynchronous.
            if(it==m_thumbnails.end()&&m_thumbnails.size()<128)it=m_thumbnails.emplace(path,renderer->LoadTexture(path)).first;
            if(it!=m_thumbnails.end())if(auto id=renderer->GetEditorTextureId(it->second)){
                auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(id),a,b);
            }
        }
        if(item.type==AssetType::Model3D&&item.extension==".obj"){
            auto path=item.fullPath.generic_string();auto it=m_modelThumbnails.find(path);
            if(it==m_modelThumbnails.end()&&!m_thumbnailTask.valid()&&m_modelThumbnails.size()<32){m_thumbnailTaskPath=path;m_thumbnailTask=std::async(std::launch::async,[file=item.fullPath]{return EngineAsset::ReadObjThumbnail(file);});}
            if(it!=m_modelThumbnails.end()){
                auto a=ImGui::GetItemRectMin();float size=m_thumbnailSize-16;
                for(auto line:it->second)ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x+8+line[0]*size,a.y+8+line[1]*size),ImVec2(a.x+8+line[2]*size,a.y+8+line[3]*size),IM_COL32(140,205,255,180));
            }
        }
        // Double Click Action
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            if (item.isDirectory) {
                m_currentDirectory = item.fullPath;
                m_history.push_back(m_currentDirectory);
                m_historyIndex = (int)m_history.size() - 1;
                ScanDirectory(m_currentDirectory);
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                ImGui::PopID();
                break;
            } else if (item.type == AssetType::Level) {
                EngineLevel::LevelSystem::Get().LoadLevel(item.fullPath.string(), renderer, camera);
                m_statusMessage = "Loaded Level: " + item.filename;
                m_statusTimer = 4.0f;
                m_statusIsError = false;
            } else if (item.type == AssetType::Model3D && renderer) {
                DirectX::XMFLOAT3 spawnPos = { 0.0f, 1.5f, 0.0f };
                if (camera) {
                    DirectX::XMFLOAT3 cPos = camera->GetPosition();
                    DirectX::XMFLOAT3 cFwd = camera->GetForwardVector();
                    spawnPos = { cPos.x + cFwd.x * 3.0f, (std::max)(cPos.y + cFwd.y * 3.0f, 0.5f), cPos.z + cFwd.z * 3.0f };
                }
                renderer->SpawnDynamicRenderModel(item.fullPath.string().c_str(), spawnPos);
                m_statusMessage = "Spawned " + item.filename + " into scene!";
                m_statusTimer = 4.0f;
                m_statusIsError = false;
            } else if(item.extension==".prefab"&&renderer){
                auto& system=EnginePrefab::PrefabSystem::Get();auto prefab=system.LoadPrefab(item.fullPath.generic_string());
                if(prefab){auto pos=camera?camera->GetPosition():DirectX::XMFLOAT3{0,0,0};auto forward=camera?camera->GetForwardVector():DirectX::XMFLOAT3{0,0,1};pos.x+=forward.x*3;pos.z+=forward.z*3;system.InstantiateWithRenderer(prefab->GetName(),renderer,nullptr,pos);}
            } else if (item.type == AssetType::Video) {
                // Open / play video via default player
                ShellExecuteA(NULL, "open", item.fullPath.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
        }

        // Draw Card Content Over Button
        ImVec2 bMin = ImGui::GetItemRectMin();
        ImVec2 bMax = ImGui::GetItemRectMax();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Icon Header
        const char* iconTag = GetAssetIcon(item.type);
        ImVec2 textPos = ImVec2(bMin.x + 8, bMin.y + 8);
        drawList->AddText(textPos, IM_COL32(255, 255, 255, 220), iconTag);

        // Size / Type Badge
        std::string badge = item.isDirectory ? "DIR" : item.formattedSize;
        ImVec2 badgePos = ImVec2(bMin.x + 8, bMin.y + m_thumbnailSize * 0.40f);
        drawList->AddText(badgePos, IM_COL32(180, 180, 180, 200), badge.c_str());

        // Filename Footer (truncated with ellipsis if long)
        std::string displayName = item.filename;
        if (displayName.length() > 14) {
            displayName = displayName.substr(0, 11) + "...";
        }
        ImVec2 namePos = ImVec2(bMin.x + 6, bMax.y - 18);
        drawList->AddText(namePos, IM_COL32(240, 240, 240, 255), displayName.c_str());

        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        // Context Menu (Right Click)
        std::string popupId = "##ctx_" + item.filename;
        if (ImGui::BeginPopupContextItem(popupId.c_str())) {
            m_selectedAssetPath = item.fullPath;
            if (item.type == AssetType::Level) {
                if (ImGui::MenuItem("Load Level into Scene")) {
                    EngineLevel::LevelSystem::Get().LoadLevel(item.fullPath.string(), renderer, camera);
                    m_statusMessage = "Loaded Level: " + item.filename;
                    m_statusTimer = 4.0f;
                }
                if (ImGui::MenuItem("Save Scene to this Level")) {
                    EngineLevel::LevelSystem::Get().SaveLevel(item.fullPath.string(), renderer, camera);
                    m_statusMessage = "Saved Scene to: " + item.filename;
                    m_statusTimer = 4.0f;
                }
                if (ImGui::MenuItem("Export as world.pak for Server (ServerMaps)")) {
                    bool ok = EngineLevel::LevelSystem::Get().ExportLevelFileToWorldPak(item.fullPath.string(), "world.pak");
                    if (ok) {
                        m_statusMessage = "Exported " + item.filename + " -> D:/MyEngine/ServerMaps/world.pak";
                        m_statusTimer = 5.0f;
                        m_statusIsError = false;
                    } else {
                        m_statusMessage = "Error: Failed to export " + item.filename + " to ServerMaps";
                        m_statusTimer = 5.0f;
                        m_statusIsError = true;
                    }
                }
                std::string customPakName = item.name + ".pak";
                std::string customLabel = "Export as " + customPakName + " for Server (ServerMaps)";
                if (item.name != "world" && ImGui::MenuItem(customLabel.c_str())) {
                    bool ok = EngineLevel::LevelSystem::Get().ExportLevelFileToWorldPak(item.fullPath.string(), customPakName);
                    if (ok) {
                        m_statusMessage = "Exported " + item.filename + " -> D:/MyEngine/ServerMaps/" + customPakName;
                        m_statusTimer = 5.0f;
                        m_statusIsError = false;
                    } else {
                        m_statusMessage = "Error: Failed to export " + item.filename + " to ServerMaps";
                        m_statusTimer = 5.0f;
                        m_statusIsError = true;
                    }
                }
                if (ImGui::MenuItem("Open Level Editor Window")) {
                    EngineLevel::LevelSystem::Get().SetOpen(true);
                }
                ImGui::Separator();
            }

            if (item.type == AssetType::Model3D && renderer) {
                if (ImGui::MenuItem("Spawn into Scene")) {
                    DirectX::XMFLOAT3 spawnPos = { 0.0f, 1.5f, 0.0f };
                    if (camera) {
                        DirectX::XMFLOAT3 cPos = camera->GetPosition();
                        DirectX::XMFLOAT3 cFwd = camera->GetForwardVector();
                        spawnPos = { cPos.x + cFwd.x * 3.0f, (std::max)(cPos.y + cFwd.y * 3.0f, 0.5f), cPos.z + cFwd.z * 3.0f };
                    }
                    renderer->SpawnDynamicRenderModel(item.fullPath.string().c_str(), spawnPos);
                    m_statusMessage = "Spawned " + item.filename;
                    m_statusTimer = 4.0f;
                }
            }

            if (item.type == AssetType::Video) {
                if (ImGui::MenuItem("Re-transcode to AV1")) {
                    TranscodeVideoToAV1(item.fullPath, m_currentDirectory);
                }
                if (ImGui::MenuItem("Play Video")) {
                    ShellExecuteA(NULL, "open", item.fullPath.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
                }
            }

            if (ImGui::MenuItem("Show in Explorer")) {
                std::string selectCmd = "/select,\"" + item.fullPath.string() + "\"";
                ShellExecuteA(NULL, "open", "explorer.exe", selectCmd.c_str(), NULL, SW_SHOWNORMAL);
            }

            if (ImGui::MenuItem("Copy Relative Path")) {
                ImGui::SetClipboardText(item.relativePath.c_str());
                m_statusMessage = "Copied to clipboard: " + item.relativePath;
                m_statusTimer = 3.0f;
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Delete Asset")) {
                deletePath = item.fullPath;
                deleteName = item.filename;
            }

            ImGui::EndPopup();
        }

        ImGui::PopID();
        itemIdx++;
    }

    if (!deletePath.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(deletePath, ec);
        Refresh();
        m_statusMessage = "Deleted " + deleteName;
        m_statusTimer = 4.0f;
    }

    // Window background right-click context menu (empty space)
    if (ImGui::BeginPopupContextWindow("##ContentBrowserBgCtx", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem(" Refresh Directory")) {
            Refresh();
        }
        if (ImGui::MenuItem(" Open Current Folder in Explorer")) {
            ShellExecuteA(NULL, "open", m_currentDirectory.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(" Create New Empty .level Map")) {
            EngineLevel::LevelSystem::Get().NewLevel(renderer, camera);
            EngineLevel::LevelSystem::Get().SaveLevel("new_map.level", renderer, camera);
            Refresh();
            m_statusMessage = "Created and loaded new_map.level";
            m_statusTimer = 4.0f;
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();
}

void ContentBrowser::DrawTranscodingModal() {
    if (!m_showTranscodingModal) return;

    ImGui::OpenPopup("AV1 Video Transcoder");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 360), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("AV1 Video Transcoder", &m_showTranscodingModal, ImGuiWindowFlags_NoResize)) {
        if (m_isTranscoding) {
            ImGui::Text("Transcoding '%s' to AV1 (.av1.mp4)...", m_transcodingFileName.c_str());
            ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(-1.0f, 0.0f), "Encoding AV1 stream...");
        } else {
            if (m_transcodeSuccess) {
                ImGui::TextColored(ImVec4(0.2f, 0.85f, 0.3f, 1.0f), "AV1 Transcoding SUCCESSFUL! Saved to project Content/Videos/");
            } else {
                ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.2f, 1.0f), "Transcoding FAILED! See logs below:");
            }
        }

        ImGui::Separator();
        ImGui::BeginChild("TranscodeLogRegion", ImVec2(0, -36), true);
        ImGui::TextUnformatted(m_transcodeLogs.c_str());
        if (m_isTranscoding) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        if (!m_isTranscoding) {
            if (ImGui::Button("Close", ImVec2(120, 0))) {
                m_showTranscodingModal = false;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
}

void ContentBrowser::DrawUI(Renderer* renderer, Camera* camera) {
#ifndef ENGINE_RELEASE
    if (!m_initialized) {
        Initialize(renderer);
    }

    if (!m_isOpen) return;

    // Decay status message timer
    if (m_statusTimer > 0.0f) {
        m_statusTimer -= ImGui::GetIO().DeltaTime;
        if (m_statusTimer <= 0.0f) {
            m_statusMessage.clear();
        }
    }

    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Content Browser (project Content)", &m_isOpen)) {
        DrawToolbar(renderer, camera);
        ImGui::Separator();
        DrawBreadcrumbs();
        ImGui::Separator();

        // Split View: Left Folder Tree (25%), Right Asset Grid (75%)
        ImGui::Columns(2, "ContentBrowserSplitter", true);
        ImGui::SetColumnWidth(0, 180.0f);

        // Left Panel
        ImGui::TextDisabled("PROJECT FOLDERS");
        ImGui::BeginChild("FolderTreeScrollArea", ImVec2(0, -28), false);
        DrawDirectoryTree(m_rootContentPath);
        ImGui::EndChild();

        ImGui::NextColumn();

        // Right Panel
        DrawAssetGrid(renderer, camera);

        ImGui::Columns(1);

        // Bottom Status Bar
        ImGui::Separator();
        ImGui::TextDisabled("Items: %d", (int)m_currentItems.size());
        ImGui::SameLine();
        ImGui::TextDisabled("| Path: %s", m_currentDirectory.string().c_str());

        if (!m_statusMessage.empty()) {
            ImGui::SameLine();
            ImVec4 statusCol = m_statusIsError ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.3f, 0.9f, 0.4f, 1.0f);
            ImGui::TextColored(statusCol, "| %s", m_statusMessage.c_str());
        }
    }
    ImGui::End();

    DrawTranscodingModal();
#endif
}

} // namespace EngineUI
