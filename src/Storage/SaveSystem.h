#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace EngineSave {

struct SavePackage {
    std::string sceneData;     // Entity states, lighting, sky, models, physx, cloth, hair, rain
    std::string imguiSettings; // ImGui window layouts, docking, sizes
};

class SaveManager {
public:
    static SaveManager& Get();

    // Get canonical path for Save1.save in STEAMUSER directory
    std::string GetSaveFilePath(const std::string& saveSlotName = "Save1.save");

    // Save combined scene + imgui settings into an encrypted & compressed Save1.save
    bool SaveGame(const std::string& sceneContent, const std::string& imguiContent, const std::string& saveSlotName = "Save1.save");

    // Load and decrypt + decompress Save1.save
    bool LoadGame(SavePackage& outPackage, const std::string& saveSlotName = "Save1.save");

    // Check if Save1.save exists
    bool SaveExists(const std::string& saveSlotName = "Save1.save");

    // Clean up legacy loose files (.ini, .txt, .json) from STEAMUSER and Config folders
    void CleanupLegacyFiles();

private:
    SaveManager() = default;
    ~SaveManager() = default;
};

} // namespace EngineSave
