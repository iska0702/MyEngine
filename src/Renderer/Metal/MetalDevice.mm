#import "MetalDevice.h"
#import <Metal/Metal.h>
#import <iostream>

namespace EngineRender {

MetalDevice::MetalDevice() = default;

MetalDevice::~MetalDevice() {
    Shutdown();
}

bool MetalDevice::Initialize() {
    @autoreleasepool {
        m_device = MTLCreateSystemDefaultDevice();
        if (!m_device) {
            std::cerr << "[Metal ERROR] Failed to create system default Metal device!" << std::endl;
            return false;
        }

        m_commandQueue = [m_device newCommandQueue];
        if (!m_commandQueue) {
            std::cerr << "[Metal ERROR] Failed to create Metal command queue!" << std::endl;
            return false;
        }

        // Capability Detection for Apple Silicon (M1/M2/M3/M4)
        m_capabilities.deviceName = [[m_device name] UTF8String];
        m_capabilities.apiVersion = "Metal 3.0";

        m_capabilities.SupportsUnifiedMemory = [m_device hasUnifiedMemory];
        m_capabilities.SupportsMemorylessRenderTargets = YES; // Apple Silicon TBDR always supports memoryless

#if defined(MAC_OS_X_VERSION_13_0) && MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_13_0
        if (@available(macOS 13.0, *)) {
            m_capabilities.SupportsMeshShaders = [m_device supportsFamily:MTLGPUFamilyMetal3];
            m_capabilities.SupportsMetalFX = YES; // Spatial and Temporal upscaling available on Apple Silicon macOS 13+
        }
#endif

        if (@available(macOS 11.0, *)) {
            m_capabilities.SupportsRayTracing = [m_device supportsRaytracing];
            m_capabilities.SupportsArgumentBuffers = YES;
            m_capabilities.SupportsIndirectCommandBuffers = YES;
        }

        m_capabilities.SupportsBCTextures = YES; // Apple Silicon macOS natively supports BC texture compression
        m_capabilities.SupportsMSAA = YES;
        m_capabilities.SupportsHDR = YES;

        std::cout << "[Metal] Initialized Device: " << m_capabilities.deviceName << std::endl;
        std::cout << "        Unified Memory: " << (m_capabilities.SupportsUnifiedMemory ? "YES" : "NO") << std::endl;
        std::cout << "        Hardware Ray Tracing: " << (m_capabilities.SupportsRayTracing ? "YES" : "NO") << std::endl;
        std::cout << "        MetalFX Upscaling: " << (m_capabilities.SupportsMetalFX ? "YES" : "NO") << std::endl;
        std::cout << "        Mesh Shaders: " << (m_capabilities.SupportsMeshShaders ? "YES" : "NO") << std::endl;

        return true;
    }
}

void MetalDevice::Shutdown() {
    @autoreleasepool {
        m_defaultLibrary = nil;
        m_commandQueue = nil;
        m_device = nil;
    }
}

id<MTLLibrary> MetalDevice::CompileSource(const std::string& source, std::string& outError) {
    @autoreleasepool {
        if (!m_device) return nil;

        NSError* error = nil;
        NSString* srcString = [NSString stringWithUTF8String:source.c_str()];
        MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
        options.fastMathEnabled = YES;
        if (@available(macOS 13.0, *)) {
            options.languageVersion = MTLLanguageVersion3_0;
        }

        id<MTLLibrary> library = [m_device newLibraryWithSource:srcString options:options error:&error];
        if (error) {
            outError = [[error localizedDescription] UTF8String];
            std::cerr << "[Metal Shader Compile Error]: " << outError << std::endl;
        }
        return library;
    }
}

} // namespace EngineRender
