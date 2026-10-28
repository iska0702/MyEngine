#include "Storage/DirectStorage/DirectStorageSystem.h"
#include "Storage/Crypto.h"
#include "Storage/ContentIO.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
static void Check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
int wmain(){try{
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    const auto root=std::filesystem::path(exe).parent_path();const auto runtime=root/L"ThirdParty/DirectStorage";
    auto cookie=AddDllDirectory(runtime.c_str());Check(cookie!=nullptr,"DLL directory unavailable");
    ComPtr<ID3D12Device> device;Check(SUCCEEDED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device))),"D3D12 device failed");
    EngineStorage::DirectStorageSystem storage;Check(storage.Initialize(device.Get()),"DirectStorage initialization failed");
    ComPtr<ID3D12Resource> texture;uint32_t width=0,height=0,mips=0;
    Check(storage.ReadTexture(L"Content/DemoTexture/demo_texture.jpg",texture.GetAddressOf(),width,height,mips),"Authenticated GDeflate GPU upload failed");
    Check(width==72&&height==48&&mips==7&&EngineStorage::streamingTextureReads==1,"Unexpected streaming texture result");
    Check(storage.HasGpuDecompression(),"GPU decompression was not active on this machine");
    storage.Shutdown();texture.Reset();
    const auto pak=root/L"Contents/content10.pak";std::ifstream input(pak,std::ios::binary);EngineStorage::StreamingPakHeader header{};
    input.read(reinterpret_cast<char*>(&header),sizeof(header));uint8_t iv[16]{};input.read(reinterpret_cast<char*>(iv),16);
    std::vector<uint8_t> encrypted(header.numEntries*160+16),table;input.read(reinterpret_cast<char*>(encrypted.data()),encrypted.size());input.close();
    auto key=PakCrypto::GetMasterKey();auto context="stream-index/2/"+std::to_string(header.numEntries)+"/"+std::to_string(header.headerSize);
    Check(PakCrypto::DecryptBlock(key.data(),32,iv,16,encrypted.data(),encrypted.size(),table,context),"Streaming table authentication failed");
    Check(table.size()>=160,"Streaming entry missing");EngineStorage::StreamingPakEntry entry{};std::memcpy(&entry,table.data(),160);
    auto flip=[&]{std::fstream file(pak,std::ios::binary|std::ios::in|std::ios::out);file.seekg(entry.offset+16);char value=0;file.read(&value,1);Check(bool(file),"Tamper fixture read failed");value^=1;file.seekp(entry.offset+16);file.write(&value,1);Check(bool(file),"Tamper fixture write failed");};
    flip();bool rejected=false;
    try{Check(storage.Initialize(device.Get()),"Tampered payload index should remain valid");rejected=!storage.ReadTexture(L"Content/DemoTexture/demo_texture.jpg",texture.GetAddressOf(),width,height,mips)&&!texture;storage.Shutdown();}catch(...){storage.Shutdown();flip();throw;}
    flip();Check(rejected,"Tampered GDeflate payload reached GPU");RemoveDllDirectory(cookie);
    std::cout<<"PASS: authenticated GDeflate read, AES-GCM CPU decryption, DirectStorage memory-to-GPU decompression, expected dimensions/mips, modified payload rejected before GPU upload\n";
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
