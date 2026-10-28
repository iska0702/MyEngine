#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace EngineAsset {

enum class AssetType : uint32_t {
    Unknown = 0,
    Model,        // .obj, .fbx, .gltf, .glb
    Texture,      // .dds, .png, .jpg, .tga, .bmp
    Audio,        // .wav, .mp3, .ogg, .flac
    Script,       // .lua
    Level,        // .level
    Prefab,       // .prefab
    Material,     // .mat
    Shader,       // .hlsl, .cso
    Animation     // .ozz, .anim
};

inline const char* AssetTypeToString(AssetType type) {
    switch (type) {
        case AssetType::Model:     return "Model";
        case AssetType::Texture:   return "Texture";
        case AssetType::Audio:     return "Audio";
        case AssetType::Script:    return "Script";
        case AssetType::Level:     return "Level";
        case AssetType::Prefab:    return "Prefab";
        case AssetType::Material:  return "Material";
        case AssetType::Shader:    return "Shader";
        case AssetType::Animation: return "Animation";
        default:                   return "Unknown";
    }
}

inline const char* AssetTypeToIcon(AssetType type) {
    switch (type) {
        case AssetType::Model:     return "[Mesh]";
        case AssetType::Texture:   return "[Tex]";
        case AssetType::Audio:     return "[Sfx]";
        case AssetType::Script:    return "[Lua]";
        case AssetType::Level:     return "[Lvl]";
        case AssetType::Prefab:    return "[Pfb]";
        case AssetType::Material:  return "[Mat]";
        case AssetType::Shader:    return "[Shd]";
        case AssetType::Animation: return "[Anim]";
        default:                   return "[File]";
    }
}

inline AssetType GetAssetTypeFromExtension(const std::string& ext) {
    std::string lowerExt = ext;
    std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
    if (lowerExt.empty()) return AssetType::Unknown;
    if (lowerExt.front() == '.') lowerExt = lowerExt.substr(1);

    if (lowerExt == "obj" || lowerExt == "fbx" || lowerExt == "gltf" || lowerExt == "glb")
        return AssetType::Model;
    if (lowerExt == "dds" || lowerExt == "png" || lowerExt == "jpg" || lowerExt == "jpeg" || lowerExt == "tga" || lowerExt == "bmp")
        return AssetType::Texture;
    if (lowerExt == "wav" || lowerExt == "mp3" || lowerExt == "ogg" || lowerExt == "flac")
        return AssetType::Audio;
    if (lowerExt == "lua")
        return AssetType::Script;
    if (lowerExt == "level")
        return AssetType::Level;
    if (lowerExt == "prefab")
        return AssetType::Prefab;
    if (lowerExt == "mat" || lowerExt == "material")
        return AssetType::Material;
    if (lowerExt == "hlsl" || lowerExt == "cso")
        return AssetType::Shader;
    if (lowerExt == "ozz" || lowerExt == "anim")
        return AssetType::Animation;

    return AssetType::Unknown;
}

struct AssetMetadata {
    std::string guid;                      // 128-bit hex GUID string, e.g. "a3f8c091-..."
    std::string virtualPath;               // Engine relative virtual path: "models/cube.obj"
    std::string diskPath;                  // Disk filepath: "src/Content/models/cube.obj"
    std::string name;                      // Display name: "cube.obj"
    AssetType type = AssetType::Unknown;
    uint64_t fileSize = 0;
    uint64_t lastModifiedTime = 0;
    std::vector<std::string> dependencies; // GUIDs or paths of dependencies
    bool isLoaded = false;
};

} // namespace EngineAsset
