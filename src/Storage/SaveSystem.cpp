#include "SaveSystem.h"
#include "Core/Project.h"
#if defined(_WIN32) && defined(ENGINE_RELEASE)
#include "Core/PackagedProject.h"
#endif
#include "Crypto.h"
#include "PakCompression.h"
#include "Platform/PlatformUtils.h"
#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#endif
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <random>
#include <mutex>

namespace EngineSave {
static std::filesystem::path SavePath(const std::string& path){return EngineProject::active?EngineProject::Path(path):std::filesystem::path(path);}

SaveManager& SaveManager::Get() {
    static SaveManager s_instance;
    return s_instance;
}

std::string SaveManager::GetSaveFilePath(const std::string& saveSlotName) {
    if(EngineProject::active && saveSlotName.rfind("EditorRecovery",0)==0 && saveSlotName.find_first_of("/\\:")==std::string::npos){
        auto file=EngineProject::current.Root()/"Saved/Recovery"/saveSlotName;
        std::filesystem::create_directories(file.parent_path());return EngineProject::Utf8(file);
    }
    if(EngineProject::active && saveSlotName=="Save1.save"){
#if defined(ENGINE_RELEASE)
#if defined(_WIN32)
        auto file=(EngineProject::runtimeUserRoot.empty()?EngineProject::current.Root():EngineProject::runtimeUserRoot)/"Saved/Runtime.save";
#else
        auto file=EngineProject::current.Root()/"Saved/Runtime.save";
#endif
#else
        auto file=EngineProject::current.Resolve(EngineProject::current.editorScene);
#endif
        std::filesystem::create_directories(file.parent_path());return EngineProject::Utf8(file);
    }
    std::filesystem::path dir = EnginePlatform::PlatformUtils::GetEngineUserDirectory();
    return (dir / saveSlotName).string();
}

bool SaveManager::SaveExists(const std::string& saveSlotName) {
    std::string path = GetSaveFilePath(saveSlotName);
    std::error_code ec;
    return std::filesystem::exists(SavePath(path), ec);
}

void SaveManager::CleanupLegacyFiles() {
    // Clean up loose files from STEAMUSER folder
    std::vector<std::string> looseFiles = {
        "bone_ai_model.json",
        "imgui.ini",
        "scene_save.txt"
    };

    if(EngineProject::active)return;
    std::filesystem::path dir = EnginePlatform::PlatformUtils::GetEngineUserDirectory();
    for (const auto& file : looseFiles) {
        std::error_code ec;
        std::filesystem::remove(dir / file, ec);
    }

    // Also clean up local build/Config loose files
    for (const auto& file : looseFiles) {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path("Config") / file, ec);
        std::filesystem::remove(std::filesystem::path("../Config") / file, ec);
    }
}

bool SaveManager::SaveGame(const std::string& sceneContent, const std::string& imguiContent, const std::string& saveSlotName) {
    static std::mutex saveMutex;
    std::lock_guard<std::mutex> lock(saveMutex);
    if (sceneContent.size() + imguiContent.size() > 50 * 1024 * 1024) return false;
    // 1. Pack Plaintext Payload
    std::ostringstream ss;
    ss << "===SCENE_START===\n"
       << sceneContent
       << "\n===SCENE_END===\n"
       << "===IMGUI_START===\n"
       << imguiContent
       << "\n===IMGUI_END===\n";

    std::string payload = ss.str();
    if (payload.empty()) return false;

    // 2. Compress with Deflate (RFC 1951)
    std::vector<uint8_t> compressed;
    if (!PakCompression::CompressDeflate((const uint8_t*)payload.data(), payload.size(), compressed)) {
        return false;
    }

    // SAVE v2 authenticates payload and size metadata before decompression.
    uint8_t iv[16];uint64_t temporaryId=0;
    if(!PakCrypto::RandomBytes(iv,sizeof(iv))||!PakCrypto::RandomBytes(&temporaryId,sizeof(temporaryId)))return false;
    auto masterKey=PakCrypto::GetMasterKey();
    std::vector<uint8_t> cipherData;
    const auto context="save/2/"+std::to_string(payload.size())+"/"+std::to_string(compressed.size());
    if(!PakCrypto::EncryptBlock(masterKey.data(),masterKey.size(),iv,16,compressed.data(),compressed.size(),cipherData,context))return false;

    // 5. Write Encrypted Binary File Save1.save
    std::string filePath = GetSaveFilePath(saveSlotName);
    std::filesystem::path parentDir = SavePath(filePath).parent_path();
    std::error_code ec;
    std::filesystem::create_directories(parentDir, ec);
    if (ec) return false;

    const std::string temporaryPath = filePath + ".tmp." + std::to_string(temporaryId);
    const std::string backupPath = filePath + ".bak";
#if defined(ENGINE_EDITOR)
    if(saveSlotName=="Save1.save" && std::filesystem::exists(SavePath(filePath),ec)){
        for(int i=4;i>=1;--i){auto older=SavePath(filePath+".version"+std::to_string(i));if(std::filesystem::exists(older,ec))std::filesystem::copy_file(older,SavePath(filePath+".version"+std::to_string(i+1)),std::filesystem::copy_options::overwrite_existing,ec);}
        std::filesystem::copy_file(SavePath(filePath),SavePath(filePath+".version1"),std::filesystem::copy_options::overwrite_existing,ec);
    }
#endif
    std::ofstream outFile(SavePath(temporaryPath), std::ios::binary | std::ios::trunc);
    if (!outFile.is_open()) return false;

    uint32_t magic = 0x45564153; // 'SAVE'
    uint32_t version = 2;
    uint32_t plainLen = (uint32_t)payload.size();
    uint32_t compressedLen = (uint32_t)compressed.size();
    uint32_t cipherLen = (uint32_t)cipherData.size();

    outFile.write((const char*)&magic, 4);
    outFile.write((const char*)&version, 4);
    outFile.write((const char*)iv, 16);
    outFile.write((const char*)&plainLen, 4);
    outFile.write((const char*)&compressedLen, 4);
    outFile.write((const char*)&cipherLen, 4);
    outFile.write((const char*)cipherData.data(), cipherData.size());
    outFile.flush();
    bool written = outFile.good();
    outFile.close();
    written = written && !outFile.fail();
    if (!written) { std::filesystem::remove(temporaryPath, ec); return false; }
#if defined(_WIN32)
    const auto target = SavePath(filePath).wstring();
    const auto temporary = SavePath(temporaryPath).wstring();
    const auto backup = SavePath(backupPath).wstring();
    bool replaced = false;
    if (std::filesystem::exists(SavePath(filePath), ec)) {
        replaced = ReplaceFileW(target.c_str(), temporary.c_str(), backup.c_str(), 0, nullptr, nullptr) != FALSE;
    } else {
        replaced = MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!replaced) { std::filesystem::remove(temporaryPath, ec); return false; }
#else
    if (std::filesystem::exists(SavePath(filePath), ec)) {
        std::filesystem::copy_file(SavePath(filePath), SavePath(backupPath), std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) { std::filesystem::remove(temporaryPath, ec); return false; }
    }
    std::filesystem::rename(SavePath(temporaryPath), SavePath(filePath), ec);
    if (ec) { std::filesystem::remove(temporaryPath, ec); return false; }
#endif

    // Keep legacy assets intact; saving a slot must not delete unrelated files.
    return true;
}

bool SaveManager::LoadGame(SavePackage& outPackage, const std::string& saveSlotName) {
    std::string filePath = GetSaveFilePath(saveSlotName);
#if defined(ENGINE_RELEASE)
    if(EngineProject::active && saveSlotName=="Save1.save" && !std::filesystem::exists(SavePath(filePath)))filePath=EngineProject::Utf8(EngineProject::current.Resolve(EngineProject::current.editorScene));
#endif
    std::ifstream inFile(SavePath(filePath), std::ios::binary);
    if (!inFile.is_open() && !EngineProject::active) {
        // Fallback search
        std::vector<std::string> fallbacks = {
            "Save1.save",
            "Config/Save1.save",
            "../Save1.save"
        };
        for (const auto& fb : fallbacks) {
            inFile.open(fb, std::ios::binary);
            if (inFile.is_open()) break;
        }
        if (!inFile.is_open()) return false;
    }

    uint32_t magic = 0;
    inFile.read((char*)&magic, 4);
    if (magic != 0x45564153) { // 'SAVE'
        return false;
    }

    uint32_t version = 0;
    inFile.read((char*)&version, 4);
    if (version != 1 && version != 2) return false;
#if defined(ENGINE_RELEASE)
    // CBC is accepted only inside the authenticated initial packaged scene, never as a runtime save downgrade.
    if(version==1 && (!EngineProject::active || SavePath(filePath)!=EngineProject::current.Resolve(EngineProject::current.editorScene)))return false;
#endif

    uint8_t iv[16];
    inFile.read((char*)iv, 16);

    uint32_t plainLen = 0;
    uint32_t compressedLen = 0;
    uint32_t cipherLen = 0;
    inFile.read((char*)&plainLen, 4);
    inFile.read((char*)&compressedLen, 4);
    inFile.read((char*)&cipherLen, 4);

    if (!inFile || plainLen == 0 || plainLen > 51 * 1024 * 1024 ||
        compressedLen == 0 || compressedLen > 60 * 1024 * 1024 ||
        cipherLen == 0 || cipherLen > 60 * 1024 * 1024) return false;

    std::vector<uint8_t> cipherData(cipherLen);
    inFile.read((char*)cipherData.data(), cipherLen);
    if (!inFile || inFile.peek()!=std::char_traits<char>::eof()) return false;
    inFile.close();

    auto masterKey=version==1?PakCrypto::GetLegacyKey():PakCrypto::GetMasterKey();
    std::vector<uint8_t> decryptedCompressed;
    bool decrypted=false;
    if(version==2){
        const auto context="save/2/"+std::to_string(plainLen)+"/"+std::to_string(compressedLen);
        decrypted=PakCrypto::DecryptBlock(masterKey.data(),masterKey.size(),iv,16,cipherData.data(),cipherData.size(),decryptedCompressed,context);
    }else decrypted=PakCrypto::DecryptAES256CBC(masterKey.data(),masterKey.size(),iv,16,cipherData.data(),cipherData.size(),decryptedCompressed);
    if(!decrypted||decryptedCompressed.size()!=compressedLen)return false;

    // 2. Decompress Deflate
    std::vector<uint8_t> uncompressedPlain;
    if (!PakCompression::DecompressDeflate(decryptedCompressed.data(), decryptedCompressed.size(), plainLen, uncompressedPlain)) {
        if(version!=1||decryptedCompressed.size()!=plainLen)return false;
        uncompressedPlain = decryptedCompressed;
    }

    if(uncompressedPlain.size()!=plainLen)return false;
    std::string fullPayload(uncompressedPlain.begin(), uncompressedPlain.end());
    SavePackage loaded;

    // 3. Parse Sections
    size_t sceneStart = fullPayload.find("===SCENE_START===\n");
    size_t sceneEnd = fullPayload.find("\n===SCENE_END===");
    if (sceneStart != std::string::npos && sceneEnd != std::string::npos && sceneEnd >= sceneStart + 18) {
        sceneStart += 18;
        loaded.sceneData = fullPayload.substr(sceneStart, sceneEnd - sceneStart);
    } else return false;

    size_t imguiStart = fullPayload.find("===IMGUI_START===\n");
    size_t imguiEnd = fullPayload.find("\n===IMGUI_END===");
    if (imguiStart != std::string::npos && imguiEnd != std::string::npos && imguiEnd >= imguiStart + 18) {
        imguiStart += 18;
        loaded.imguiSettings = fullPayload.substr(imguiStart, imguiEnd - imguiStart);
    } else return false;
    outPackage = std::move(loaded);

    return true;
}

} // namespace EngineSave
