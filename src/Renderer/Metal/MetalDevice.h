#pragma once

#include "../Common/GraphicsCapabilities.h"
#include <string>
#include <memory>

#ifdef __OBJC__
#import <Metal/Metal.h>
#else
typedef void* id;
#endif

namespace EngineRender {

class MetalDevice {
public:
    MetalDevice();
    ~MetalDevice();

    bool Initialize();
    void Shutdown();

#ifdef __OBJC__
    id<MTLDevice> GetDevice() const { return m_device; }
    id<MTLCommandQueue> GetCommandQueue() const { return m_commandQueue; }
    id<MTLLibrary> GetDefaultLibrary() const { return m_defaultLibrary; }
#else
    void* GetDevice() const { return m_device; }
    void* GetCommandQueue() const { return m_commandQueue; }
    void* GetDefaultLibrary() const { return m_defaultLibrary; }
#endif

    const GraphicsCapabilities& GetCapabilities() const { return m_capabilities; }

    // Shader library management
#ifdef __OBJC__
    id<MTLLibrary> CompileSource(const std::string& source, std::string& outError);
#else
    void* CompileSource(const std::string& source, std::string& outError);
#endif

private:
#ifdef __OBJC__
    id<MTLDevice> m_device = nil;
    id<MTLCommandQueue> m_commandQueue = nil;
    id<MTLLibrary> m_defaultLibrary = nil;
#else
    void* m_device = nullptr;
    void* m_commandQueue = nullptr;
    void* m_defaultLibrary = nullptr;
#endif

    GraphicsCapabilities m_capabilities;
};

} // namespace EngineRender
