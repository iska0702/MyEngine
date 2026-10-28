#import "MetalBuffer.h"
#import <Metal/Metal.h>
#import <iostream>

namespace EngineRender {

MetalBuffer::MetalBuffer() = default;

MetalBuffer::~MetalBuffer() {
    Destroy();
}

bool MetalBuffer::Initialize(id<MTLDevice> device, size_t size, BufferStorageMode mode, const void* data, const std::string& label) {
    @autoreleasepool {
        if (!device || size == 0) return false;
        Destroy();

        m_size = size;
        m_mode = mode;

        MTLResourceOptions options = (mode == BufferStorageMode::Shared)
            ? MTLResourceStorageModeShared
            : MTLResourceStorageModePrivate;

        if (data && mode == BufferStorageMode::Shared) {
            m_buffer = [device newBufferWithBytes:data length:size options:options];
        } else {
            m_buffer = [device newBufferWithLength:size options:options];
        }

        if (!m_buffer) {
            std::cerr << "[Metal ERROR] Failed to allocate buffer of size " << size << std::endl;
            return false;
        }

        if (!label.empty()) {
            [m_buffer setLabel:[NSString stringWithUTF8String:label.c_str()]];
        }

        return true;
    }
}

void MetalBuffer::Destroy() {
    @autoreleasepool {
        m_buffer = nil;
        m_size = 0;
    }
}

void MetalBuffer::UpdateData(const void* data, size_t size, size_t offset) {
    if (!m_buffer || !data || (offset + size) > m_size) return;

    if (m_mode == BufferStorageMode::Shared) {
        uint8_t* dst = (uint8_t*)[m_buffer contents] + offset;
        std::memcpy(dst, data, size);
    }
}

void* MetalBuffer::GetContents() const {
    if (m_buffer && m_mode == BufferStorageMode::Shared) {
        return [m_buffer contents];
    }
    return nullptr;
}

} // namespace EngineRender
