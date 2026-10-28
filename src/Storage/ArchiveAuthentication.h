#pragma once
#include "Crypto.h"
#include <fstream>
#include <filesystem>
namespace EngineStorage {
// Authenticate bounded indices at startup. Each payload is authenticated again when read.
inline bool AuthenticateArchiveHeaders(const std::filesystem::path& directory) {
    auto key=PakCrypto::GetMasterKey();
    for(unsigned volume=1;volume<=10;++volume){
        auto name="content"+std::string(volume<10?"0":"")+std::to_string(volume)+".pak";
        std::ifstream file(directory/name,std::ios::binary|std::ios::ate);
        if(!file)return false;auto end=file.tellg();if(end<16)return false;file.seekg(0);
        uint32_t magic=0;file.read(reinterpret_cast<char*>(&magic),4);
        uint32_t count=0,indexBytes=0,plainBytes=0;std::string context="index";
        uint8_t iv[16]{};
        if(volume<10){
            if(magic!=0x334B5045)return false;
            file.read(reinterpret_cast<char*>(iv),16);file.read(reinterpret_cast<char*>(&indexBytes),4);file.read(reinterpret_cast<char*>(&plainBytes),4);
            if(indexBytes<20||indexBytes>20*1024*1024||indexBytes!=uint64_t(plainBytes)+16||uint64_t(indexBytes)+28>uint64_t(end))return false;
        }else{
            uint32_t version=0,headerBytes=0;file.read(reinterpret_cast<char*>(&version),4);file.read(reinterpret_cast<char*>(&count),4);file.read(reinterpret_cast<char*>(&headerBytes),4);
            if(magic!=0x334B5053||version!=2||count>65536)return false;
            plainBytes=count*160;indexBytes=plainBytes+16;
            if(uint64_t(plainBytes)+48>headerBytes||headerBytes>uint64_t(end))return false;
            context="stream-index/2/"+std::to_string(count)+"/"+std::to_string(headerBytes);
            file.read(reinterpret_cast<char*>(iv),16);
        }
        std::vector<uint8_t> encrypted(indexBytes),plain;file.read(reinterpret_cast<char*>(encrypted.data()),encrypted.size());
        if(!file||!PakCrypto::DecryptBlock(key.data(),key.size(),iv,16,encrypted.data(),encrypted.size(),plain,context)||plain.size()!=plainBytes)return false;
        if(volume<10){if(plain.size()<4)return false;std::memcpy(&count,plain.data(),4);if(count>65536||plain.size()!=4+uint64_t(count)*276)return false;}
    }
    return true;
}
}
