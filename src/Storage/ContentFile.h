#pragma once
#include <cstring>
#include "Crypto.h"
#include "PakCompression.h"
#include "ContentIO.h"
#include <algorithm>
#include <cstring>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iterator>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace EngineStorage {
inline std::vector<uint8_t> ReadContentArchive(const std::u16string& key, const std::filesystem::path& directoryOverride={}, bool* found=nullptr) {
    if(found)*found=false;
    std::filesystem::path directory = "Contents";
#if defined(_WIN32)
    wchar_t executable[32768] {};
    const auto length = GetModuleFileNameW(nullptr, executable, 32768);
    if (length > 0 && length < 32768) directory = std::filesystem::path(executable).parent_path() / "Contents";
#elif defined(__APPLE__)
    uint32_t length = 0;
    _NSGetExecutablePath(nullptr, &length);
    std::vector<char> executable(length);
    if (_NSGetExecutablePath(executable.data(), &length) == 0)
        directory = std::filesystem::path(executable.data()).parent_path().parent_path() / "Resources/Contents";
#elif defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    directory = "/app0/Contents";
#endif
    if(!directoryOverride.empty()) directory=directoryOverride;
    const auto masterKey = PakCrypto::GetMasterKey();
    for (int i = 1; i <= 10; ++i) {
        const auto name = std::string("content") + (i < 10 ? "0" : "") + std::to_string(i) + ".pak";
        std::ifstream file(directory / name, std::ios::binary | std::ios::ate);
        if (!file) continue;
        const auto fileSize = file.tellg();
        if (fileSize < 28) continue;
        file.seekg(0);
        uint32_t magic = 0, indexSize = 0, plainSize = 0;
        uint8_t iv[16] {};
        file.read(reinterpret_cast<char*>(&magic), 4);
        file.read(reinterpret_cast<char*>(iv), 16);
        file.read(reinterpret_cast<char*>(&indexSize), 4);
        file.read(reinterpret_cast<char*>(&plainSize), 4);
        if (!file || magic != 0x334B5045 || indexSize == 0 || indexSize > 100 * 1024 * 1024 ||
            uint64_t(indexSize) + 28 > static_cast<uint64_t>(fileSize)) continue;
        std::vector<uint8_t> encrypted(indexSize), index;
        file.read(reinterpret_cast<char*>(encrypted.data()), encrypted.size());
        if (!file || !PakCrypto::DecryptBlock(masterKey.data(), masterKey.size(), iv, 16,
            encrypted.data(), encrypted.size(), index) || index.size() < 4 || index.size() != plainSize) continue;
        uint32_t count = 0;
        std::memcpy(&count, index.data(), 4);
        if (count > 65536 || index.size()!=4+uint64_t(count)*276) continue;
        for (uint32_t entry = 0; entry < count; ++entry) {
            const auto* data = index.data() + 4 + size_t(entry) * 276;
            char16_t entryName[129] {};
            std::memcpy(entryName, data, 256);
            std::u16string normalized(entryName);
            while (!normalized.empty() && normalized.back() == u' ') normalized.pop_back();
            std::replace(normalized.begin(), normalized.end(), u'\\', u'/');
            if (normalized != key) continue;
            uint32_t fields[5];
            std::memcpy(fields, data + 256, sizeof(fields));
            const auto offset = fields[0], cipherSize = fields[1], compressedSize = fields[2], rawSize = fields[3], flags = fields[4];
            if (cipherSize < 32 || offset<28+indexSize || flags>1 || compressedSize!=cipherSize-32 || (!flags&&compressedSize!=rawSize) || uint64_t(offset) + cipherSize > static_cast<uint64_t>(fileSize) ||
                rawSize > 512 * 1024 * 1024) return {};
            std::vector<uint8_t> payload;
            if(TryReadContentRange(directory/name,offset,cipherSize,payload)) {
                std::memcpy(iv,payload.data(),16);
                encrypted.assign(payload.begin()+16,payload.end());
            } else {
                file.seekg(offset);
                file.read(reinterpret_cast<char*>(iv),16);
                encrypted.resize(cipherSize-16);
                file.read(reinterpret_cast<char*>(encrypted.data()),encrypted.size());
            }
            std::vector<uint8_t> decoded;
            if (!file || !PakCrypto::DecryptBlock(masterKey.data(), masterKey.size(), iv, 16,
                encrypted.data(), encrypted.size(), decoded, PakCrypto::FileContext(normalized)) || decoded.size() != compressedSize) return {};
            if (flags & 1) {
                std::vector<uint8_t> raw;
                if (!PakCompression::DecompressDeflate(decoded.data(), decoded.size(), rawSize, raw)) return {};
                if(found)*found=true;
                return raw;
            }
            if(found)*found=true;
            return decoded;
        }
    }
    return {};
}
// Developer files may override packaged assets; installed builds read the same
// virtual Content/... names directly from the encrypted content archives.
inline bool ReadContentBytes(const std::string& path, std::vector<uint8_t>& bytes) {
    if(TryReadContentRange(std::filesystem::u8path(path),0,0,bytes))return true;
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    if (file) {
        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return !file.bad();
    }
    std::string key = path;
    std::replace(key.begin(), key.end(), '\\', '/');
    auto pos = key.find("Content/");
    if (pos != std::string::npos) key = key.substr(pos);
    else {
        pos = key.find("Contents/");
        if (pos != std::string::npos) key = key.substr(pos + 9);
        key = "Content/" + key;
    }
    bool found=false;
    bytes = ReadContentArchive(std::filesystem::u8path(key).u16string(),{},&found);
    return found;
}
}
