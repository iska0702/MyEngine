#pragma once

#include "../Common/PixelFormatMapping.h"
#include <string>
#include <cstdint>

#ifdef __OBJC__
#import <Metal/Metal.h>
#else
typedef void* id;
#endif

namespace EngineRender {

class MetalTexture {
public:
    MetalTexture();
    ~MetalTexture();

#ifdef __OBJC__
    bool Initialize2D(id<MTLDevice> device, int width, int height, PixelFormat format,
                      NSUInteger usage, bool memoryless = false, const std::string& label = "");
    bool Initialize3D(id<MTLDevice> device, int width, int height, int depth, PixelFormat format,
                      NSUInteger usage, const std::string& label = "");
    id<MTLTexture> GetTexture() const { return m_texture; }
#else
    bool Initialize2D(void* device, int width, int height, PixelFormat format,
                      uint64_t usage, bool memoryless = false, const std::string& label = "");
    bool Initialize3D(void* device, int width, int height, int depth, PixelFormat format,
                      uint64_t usage, const std::string& label = "");
    void* GetTexture() const { return m_texture; }
#endif

    void Destroy();
    void UploadData2D(const void* data, size_t bytesPerRow);

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    int GetDepth() const { return m_depth; }
    PixelFormat GetFormat() const { return m_format; }

private:
#ifdef __OBJC__
    id<MTLTexture> m_texture = nil;
#else
    void* m_texture = nullptr;
#endif
    int m_width = 0;
    int m_height = 0;
    int m_depth = 1;
    PixelFormat m_format = PixelFormat::Unknown;
};

} // namespace EngineRender
