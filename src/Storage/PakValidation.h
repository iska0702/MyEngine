#pragma once
#include <cstdint>
namespace EngineStorage {
inline constexpr uint32_t EncryptedPakMagic=0x334B5045;
inline constexpr uint32_t StreamingPakMagic=0x334B5053;
inline bool ValidLegacyPakHeader(uint32_t count,uint64_t bytes){
    return count>0 && count<=65536 && bytes>=4+uint64_t(count)*264;
}
}
