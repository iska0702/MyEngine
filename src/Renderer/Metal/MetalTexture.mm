#import "MetalTexture.h"
#import <Metal/Metal.h>
#import <iostream>

namespace EngineRender {

MetalTexture::MetalTexture() = default;

MetalTexture::~MetalTexture() {
    Destroy();
}

void MetalTexture::Destroy() {
    @autoreleasepool {
        m_texture = nil;
        m_width = 0;
        m_height = 0;
        m_depth = 1;
    }
}

bool MetalTexture::Initialize2D(id<MTLDevice> device, int width, int height, PixelFormat format,
                                NSUInteger usage, bool memoryless, const std::string& label) {
    @autoreleasepool {
        if (!device || width <= 0 || height <= 0) return false;
        Destroy();

        m_width = width;
        m_height = height;
        m_depth = 1;
        m_format = format;

        MTLPixelFormat mtlFmt = (MTLPixelFormat)ToMTLPixelFormat(format);
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:mtlFmt
                                                                                       width:width
                                                                                      height:height
                                                                                   mipmapped:NO];
        desc.usage = usage;

        if (memoryless) {
            desc.storageMode = MTLStorageModeMemoryless;
        } else {
            desc.storageMode = MTLStorageModePrivate;
        }

        m_texture = [device newTextureWithDescriptor:desc];
        if (!m_texture) {
            std::cerr << "[Metal ERROR] Failed to create 2D texture " << width << "x" << height << std::endl;
            return false;
        }

        if (!label.empty()) {
            [m_texture setLabel:[NSString stringWithUTF8String:label.c_str()]];
        }

        return true;
    }
}

bool MetalTexture::Initialize3D(id<MTLDevice> device, int width, int height, int depth, PixelFormat format,
                                NSUInteger usage, const std::string& label) {
    @autoreleasepool {
        if (!device || width <= 0 || height <= 0 || depth <= 0) return false;
        Destroy();

        m_width = width;
        m_height = height;
        m_depth = depth;
        m_format = format;

        MTLPixelFormat mtlFmt = (MTLPixelFormat)ToMTLPixelFormat(format);
        MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
        desc.textureType = MTLTextureType3D;
        desc.pixelFormat = mtlFmt;
        desc.width = width;
        desc.height = height;
        desc.depth = depth;
        desc.usage = usage;
        desc.storageMode = MTLStorageModePrivate;

        m_texture = [device newTextureWithDescriptor:desc];
        if (!m_texture) {
            std::cerr << "[Metal ERROR] Failed to create 3D texture " << width << "x" << height << "x" << depth << std::endl;
            return false;
        }

        if (!label.empty()) {
            [m_texture setLabel:[NSString stringWithUTF8String:label.c_str()]];
        }

        return true;
    }
}

void MetalTexture::UploadData2D(const void* data, size_t bytesPerRow) {
    @autoreleasepool {
        if (!m_texture || !data) return;

        MTLRegion region = MTLRegionMake2D(0, 0, m_width, m_height);
        [m_texture replaceRegion:region
                     mipmapLevel:0
                       withBytes:data
                     bytesPerRow:bytesPerRow];
    }
}

} // namespace EngineRender
