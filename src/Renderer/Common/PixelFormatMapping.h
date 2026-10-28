#pragma once

#include <cstdint>

namespace EngineRender {

enum class PixelFormat : uint32_t {
    Unknown = 0,
    RGBA8_UNORM,
    RGBA8_UNORM_SRGB,
    BGRA8_UNORM,
    BGRA8_UNORM_SRGB,
    RGBA16_FLOAT,
    RGBA32_FLOAT,
    RG11B10_FLOAT,
    RG16_FLOAT,
    R32_FLOAT,
    R32_UINT,
    R8_UNORM,
    D32_FLOAT,
    D24_UNORM_S8_UINT,
    D32_FLOAT_S8_UINT,
    BC1_UNORM,
    BC1_UNORM_SRGB,
    BC3_UNORM,
    BC3_UNORM_SRGB,
    BC4_UNORM,
    BC5_UNORM,
    BC7_UNORM,
    BC7_UNORM_SRGB
};

#if defined(__APPLE__)
// Map PixelFormat to MTLPixelFormat value constants without pulling in full Metal headers
inline uint32_t ToMTLPixelFormat(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::RGBA8_UNORM:      return 70;  // MTLPixelFormatRGBA8Unorm
        case PixelFormat::RGBA8_UNORM_SRGB: return 71;  // MTLPixelFormatRGBA8Unorm_sRGB
        case PixelFormat::BGRA8_UNORM:      return 80;  // MTLPixelFormatBGRA8Unorm
        case PixelFormat::BGRA8_UNORM_SRGB: return 81;  // MTLPixelFormatBGRA8Unorm_sRGB
        case PixelFormat::RGBA16_FLOAT:     return 115; // MTLPixelFormatRGBA16Float
        case PixelFormat::RGBA32_FLOAT:     return 125; // MTLPixelFormatRGBA32Float
        case PixelFormat::RG11B10_FLOAT:    return 92;  // MTLPixelFormatRG11B10Float
        case PixelFormat::RG16_FLOAT:       return 65;  // MTLPixelFormatRG16Float
        case PixelFormat::R32_FLOAT:        return 55;  // MTLPixelFormatR32Float
        case PixelFormat::R32_UINT:         return 53;  // MTLPixelFormatR32Uint
        case PixelFormat::R8_UNORM:         return 10;  // MTLPixelFormatR8Unorm
        case PixelFormat::D32_FLOAT:        return 252; // MTLPixelFormatDepth32Float
        case PixelFormat::D24_UNORM_S8_UINT:
        case PixelFormat::D32_FLOAT_S8_UINT:return 260; // MTLPixelFormatDepth32Float_Stencil8
        case PixelFormat::BC1_UNORM:        return 130; // MTLPixelFormatBC1_RGBA
        case PixelFormat::BC1_UNORM_SRGB:   return 131; // MTLPixelFormatBC1_RGBA_sRGB
        case PixelFormat::BC3_UNORM:        return 134; // MTLPixelFormatBC3_RGBA
        case PixelFormat::BC3_UNORM_SRGB:   return 135; // MTLPixelFormatBC3_RGBA_sRGB
        case PixelFormat::BC4_UNORM:        return 140; // MTLPixelFormatBC4_RUnorm
        case PixelFormat::BC5_UNORM:        return 142; // MTLPixelFormatBC5_RGUnorm
        case PixelFormat::BC7_UNORM:        return 152; // MTLPixelFormatBC7_RGBAUnorm
        case PixelFormat::BC7_UNORM_SRGB:   return 153; // MTLPixelFormatBC7_RGBAUnorm_sRGB
        default:                            return 0;   // MTLPixelFormatInvalid
    }
}
#endif

} // namespace EngineRender
