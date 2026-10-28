#include "Scene/SceneGraph.h"
#include "Storage/SaveSystem.h"
#include "Storage/ContentFile.h"
#include "ThirdParty/EngineDependencies/miniaudio.h"
#include <iostream>
#include <stdexcept>
#include <chrono>

static void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main() {
    namespace fs = std::filesystem;
    fs::path directory;
    try {
        auto& graph = EngineScene::SceneGraph::Get();
        auto root = graph.GetRoot();
        Check(root->GetId() == 0 && graph.FindNodeById(0) == root, "Root ID invariant broken");
        auto parent = graph.CreateNode("parent");
        auto child = graph.CreateNode("child", parent);
        for (int i = 0; i < 4; ++i) graph.CreateNode("leaf" + std::to_string(i), child);
        parent->SetParent(child.get());
        Check(parent->GetParent() == root.get(), "Hierarchy cycle accepted");
        root->SetParent(parent.get());
        Check(root->GetParent() == nullptr, "Root was reparented");
        Check(!graph.RemoveNode(0), "Root was removed");
        Check(graph.RemoveNode(parent->GetId()), "Parent removal failed");
        Check(!graph.FindNode("child"), "Child remains registered");
        for (int i = 0; i < 4; ++i) Check(!graph.FindNode("leaf" + std::to_string(i)), "Descendant remains registered");
        graph.Clear();
        directory = fs::temp_directory_path() / ("Idontknow-save-regression-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(directory);
        auto slot = (directory / "test.save").string();
        auto& saves = EngineSave::SaveManager::Get();
        Check(saves.SaveGame("scene A", "layout A", slot), "First save failed");
        EngineSave::SavePackage package;
        Check(saves.LoadGame(package, slot) && package.sceneData == "scene A" && package.imguiSettings == "layout A", "Save roundtrip failed");
        Check(saves.SaveGame("scene B", "layout B", slot), "Replacement save failed");
        Check(saves.LoadGame(package, slot) && package.sceneData == "scene B", "Replacement lost data");
        Check(saves.LoadGame(package, slot + ".bak") && package.sceneData == "scene A", "Backup lost previous save");
        fs::create_directory(directory / "blocked.save");
        Check(!saves.SaveGame("failed", "", (directory / "blocked.save").string()), "Failed replacement reported success");
        Check(saves.LoadGame(package, slot) && package.sceneData == "scene B", "Failed write damaged existing save");
        for (const auto& file : fs::directory_iterator(directory))
            Check(file.path().filename().string().find(".tmp.") == std::string::npos, "Temporary save was leaked");
        std::vector<uint8_t> data;
        Check(EngineStorage::ReadContentBytes("Content/scripts/packed.lua", data), "PAK script not found");
        Check(std::string(data.begin(), data.end()).find("PACKED_SCRIPT_OK") != std::string::npos, "PAK script bytes corrupted");
        Check(EngineStorage::ReadContentBytes("missing/src/Content/Audio/packed.wav", data), "PAK audio not found");
        ma_decoder decoder;
        Check(ma_decoder_init_memory(data.data(), data.size(), nullptr, &decoder) == MA_SUCCESS, "Packed audio cannot be decoded");
        ma_decoder_uninit(&decoder);
        fs::remove_all(directory);
        std::cout << "PASS: root ID, cycles, subtree deletion, save roundtrip, atomic replacement, backup, failed writes, packaged script and audio\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        if (!directory.empty()) { std::error_code ec; fs::remove_all(directory, ec); }
        return 1;
    }
}
