#pragma once
#include <cstdint>
namespace EngineStorage {
// Cooked RGBA8 mip chain, D3D12 GetCopyableFootprints layout, GDeflate payload.
struct StreamingTextureHeader {
    uint32_t magic=0x58545347,version=1,width=0,height=0,mips=0,format=28,rawBytes=0,compressedBytes=0;
};
static_assert(sizeof(StreamingTextureHeader)==32);
struct StreamingPakHeader {
    uint32_t magic=0x4B415053,version=1,numEntries=0,headerSize=0;
};
static_assert(sizeof(StreamingPakHeader)==16);
struct StreamingPakEntry {
    char name[128]={};
    uint64_t offset=0;
    uint32_t compressedBytes=0,rawBytes=0,width=0,height=0,mips=0,format=28;
};
static_assert(sizeof(StreamingPakEntry)==160);
}
