#pragma once
#include <filesystem>
#include <algorithm>
#include <string>
#include <stdexcept>
#include <cctype>

namespace EngineAsset {
inline std::string AssetPathUtf8(const std::filesystem::path& path) {
    auto text=path.generic_u8string(); return std::string(text.begin(),text.end());
}
inline std::string ImportTextureAsset(const std::filesystem::path& source, const std::filesystem::path& textureDirectory) {
    auto extension=source.extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension!=".png" && extension!=".jpg" && extension!=".jpeg" && extension!=".bmp" && extension!=".tga")
        throw std::runtime_error("Choose a PNG, JPG, BMP or TGA texture.");
    if(!std::filesystem::is_regular_file(source)) throw std::runtime_error("Texture file not found.");
    std::filesystem::create_directories(textureDirectory);
    auto destination=textureDirectory/source.filename();
    if(std::filesystem::exists(destination) && std::filesystem::equivalent(source,destination))
        return "Content/Textures/"+AssetPathUtf8(destination.filename());
    unsigned suffix=1;
    while(std::filesystem::exists(destination)) {
        auto filename=source.stem();filename+="_"+std::to_string(suffix++);filename+=source.extension();
        destination=textureDirectory/filename;
    }
    std::filesystem::copy_file(source,destination);
    return "Content/Textures/"+AssetPathUtf8(destination.filename());
}
}
