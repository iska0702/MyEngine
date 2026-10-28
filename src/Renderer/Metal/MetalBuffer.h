#pragma once

#include <cstdint>
#include <string>
#include <cstring>

#ifdef __OBJC__
#import <Metal/Metal.h>
#else
typedef void* id;
#endif

namespace EngineRender {

enum class BufferStorageMode {
    Shared,   // Unified memory: zero-copy CPU/GPU shared access
    Private   // GPU-only memory for static data
};

class MetalBuffer {
public:
    MetalBuffer();
    ~MetalBuffer();

#ifdef __OBJC__
    bool Initialize(id<MTLDevice> device, size_t size, BufferStorageMode mode, const void* data = nullptr, const std::string& label = "");
    id<MTLBuffer> GetBuffer() const { return m_buffer; }
#else
    bool Initialize(void* device, size_t size, BufferStorageMode mode, const void* data = nullptr, const std::string& label = "");
    void* GetBuffer() const { return m_buffer; }
#endif

    void Destroy();
    void UpdateData(const void* data, size_t size, size_t offset = 0);
    void* GetContents() const;
    size_t GetSize() const { return m_size; }

private:
#ifdef __OBJC__
    id<MTLBuffer> m_buffer = nil;
#else
    void* m_buffer = nullptr;
#endif
    size_t m_size = 0;
    BufferStorageMode m_mode = BufferStorageMode::Shared;
};

} // namespace EngineRender
