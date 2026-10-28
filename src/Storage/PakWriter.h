#pragma once
#include "Crypto.h"
#include "PakCompression.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <stdexcept>
namespace EngineStorage {
struct BuildAsset {std::u16string name;std::filesystem::path source;std::vector<uint8_t> bytes;};
inline std::vector<uint8_t> ReadBuildInput(const std::filesystem::path& path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Cannot read build input");
    auto length=file.tellg();if(length<0||length>512ll*1024*1024)throw std::runtime_error("Build input exceeds 512 MB");
    auto modified=std::filesystem::last_write_time(path);file.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(length));if(length)file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    if(!file||std::filesystem::last_write_time(path)!=modified||std::filesystem::file_size(path)!=bytes.size())throw std::runtime_error("Build input changed while reading; retry the build");
    return bytes;
}
inline void AppendU32(std::vector<uint8_t>& bytes,uint32_t value){for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(value>>(i*8)));}
inline void WriteAuthenticatedPak(const std::filesystem::path& path,const std::vector<BuildAsset>& assets,const std::function<void()>& checkpoint={}){
    if(assets.size()>65536)throw std::runtime_error("Too many entries in a content volume");
    const uint32_t indexBytes=4+static_cast<uint32_t>(assets.size())*276;
    uint64_t offset=28ull+indexBytes+16;
    std::filesystem::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary|std::ios::trunc);
    if(!file)throw std::runtime_error("Cannot create content archive");file.seekp(offset);
    std::vector<uint8_t> index;index.reserve(indexBytes);AppendU32(index,static_cast<uint32_t>(assets.size()));
    std::set<std::u16string> names;auto key=PakCrypto::GetMasterKey();
    for(const auto& asset:assets){
        if(checkpoint)checkpoint();
        if(asset.name.empty()||asset.name.size()>127||asset.name.find(u'\0')!=std::u16string::npos||!names.insert(asset.name).second)throw std::runtime_error("Invalid or duplicate archive path (maximum 127 UTF-16 units)");
        auto bytes=asset.source.empty()?asset.bytes:ReadBuildInput(asset.source);std::vector<uint8_t> compressed,cipher;
        uint32_t flags=0;
        if(bytes.size()>64&&PakCompression::CompressDeflate(bytes.data(),bytes.size(),compressed)&&compressed.size()<bytes.size())flags=1;
        const auto& input=flags?compressed:bytes;uint8_t iv[16]{};
        if(!PakCrypto::RandomBytes(iv,16)||!PakCrypto::EncryptBlock(key.data(),key.size(),iv,16,input.data(),input.size(),cipher,PakCrypto::FileContext(asset.name)))throw std::runtime_error("Cannot encrypt content archive");
        if(offset+16+cipher.size()>UINT32_MAX)throw std::runtime_error("Content volume exceeds 4 GB; split its assets");
        for(size_t i=0;i<128;++i){uint16_t ch=i<asset.name.size()?asset.name[i]:u' ';index.push_back(uint8_t(ch));index.push_back(uint8_t(ch>>8));}
        AppendU32(index,static_cast<uint32_t>(offset));AppendU32(index,static_cast<uint32_t>(16+cipher.size()));
        AppendU32(index,static_cast<uint32_t>(input.size()));AppendU32(index,static_cast<uint32_t>(bytes.size()));AppendU32(index,flags);
        file.write(reinterpret_cast<char*>(iv),16);file.write(reinterpret_cast<char*>(cipher.data()),cipher.size());offset+=16+cipher.size();
        if(!file)throw std::runtime_error("Cannot write encrypted content payload");
    }
    uint8_t iv[16]{};std::vector<uint8_t> cipher;
    if(!PakCrypto::RandomBytes(iv,16)||!PakCrypto::EncryptBlock(key.data(),key.size(),iv,16,index.data(),index.size(),cipher))throw std::runtime_error("Cannot encrypt archive index");
    file.seekp(0);uint32_t magic=0x334B5045,cipherBytes=static_cast<uint32_t>(cipher.size());
    file.write(reinterpret_cast<char*>(&magic),4);file.write(reinterpret_cast<char*>(iv),16);file.write(reinterpret_cast<char*>(&cipherBytes),4);file.write(reinterpret_cast<const char*>(&indexBytes),4);file.write(reinterpret_cast<char*>(cipher.data()),cipher.size());
    file.flush();if(!file)throw std::runtime_error("Cannot finish encrypted content archive");
}
}
