#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dstorage.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <iostream>
#include <algorithm>
#include "Storage/StreamingTexture.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"
using Microsoft::WRL::ComPtr;
int wmain(int argc,wchar_t** argv){
    if(argc!=4){std::cerr<<"CookStreamingTexture input output DirectStorageDirectory\n";return 1;}
    auto directory=std::filesystem::absolute(argv[3]);
    auto core=LoadLibraryExW((directory/L"dstoragecore.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    auto module=LoadLibraryExW((directory/L"dstorage.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!core||!module)return 2;
    auto createCodec=reinterpret_cast<decltype(&DStorageCreateCompressionCodec)>(GetProcAddress(module,"DStorageCreateCompressionCodec"));
    ComPtr<IDStorageCompressionCodec> codec;
    if(!createCodec||FAILED(createCodec(DSTORAGE_COMPRESSION_FORMAT_GDEFLATE,0,IID_PPV_ARGS(&codec))))return 3;
    std::ifstream source(std::filesystem::path(argv[1]),std::ios::binary);std::vector<uint8_t> encoded((std::istreambuf_iterator<char>(source)),{});
    int width=0,height=0,channels=0;auto* pixels=stbi_load_from_memory(encoded.data(),int(encoded.size()),&width,&height,&channels,4);
    if(!pixels||width<1||height<1||width>16384||height>16384)return 4;
    EngineStorage::StreamingTextureHeader header;header.width=width;header.height=height;header.mips=1;
    for(int w=width,h=height;w>1||h>1;){w=std::max(1,w/2);h=std::max(1,h/2);++header.mips;}
    ComPtr<ID3D12Device> device;if(FAILED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))))return 5;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;desc.DepthOrArraySize=1;desc.MipLevels=UINT16(header.mips);desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(header.mips);UINT64 bytes=0;
    device->GetCopyableFootprints(&desc,0,header.mips,0,layouts.data(),nullptr,nullptr,&bytes);
    if(bytes>256ull*1024*1024)return 6;
    std::vector<uint8_t> raw(size_t(bytes),0),mip(pixels,pixels+size_t(width)*height*4);stbi_image_free(pixels);
    int w=width,h=height;
    for(uint32_t level=0;level<header.mips;++level){
        const auto& layout=layouts[level];for(int row=0;row<h;++row)memcpy(raw.data()+layout.Offset+size_t(row)*layout.Footprint.RowPitch,mip.data()+size_t(row)*w*4,size_t(w)*4);
        if(level+1<header.mips){int nw=std::max(1,w/2),nh=std::max(1,h/2);std::vector<uint8_t> next(size_t(nw)*nh*4);stbir_resize_uint8_srgb(mip.data(),w,h,0,next.data(),nw,nh,0,STBIR_RGBA);mip.swap(next);w=nw;h=nh;}
    }
    std::vector<uint8_t> compressed(codec->CompressBufferBound(raw.size()));size_t compressedSize=0;
    if(FAILED(codec->CompressBuffer(raw.data(),raw.size(),DSTORAGE_COMPRESSION_BEST_RATIO,compressed.data(),compressed.size(),&compressedSize)))return 7;
    // Verify the cooked payload before publishing it.
    std::vector<uint8_t> verify(raw.size());size_t decoded=0;
    if(FAILED(codec->DecompressBuffer(compressed.data(),compressedSize,verify.data(),verify.size(),&decoded))||decoded!=raw.size()||verify!=raw)return 8;
    header.rawBytes=uint32_t(raw.size());header.compressedBytes=uint32_t(compressedSize);
    auto output=std::filesystem::path(argv[2]);std::filesystem::create_directories(output.parent_path());std::ofstream file(output,std::ios::binary|std::ios::trunc);
    file.write(reinterpret_cast<const char*>(&header),sizeof(header));file.write(reinterpret_cast<const char*>(compressed.data()),compressedSize);
    if(!file)return 9;
    std::cout<<width<<'x'<<height<<", "<<header.mips<<" mips, "<<raw.size()<<" -> "<<compressedSize<<" bytes; round-trip verified\n";return 0;
}
