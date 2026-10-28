#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include "Platform/PlatformTypes.h"

#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#endif

namespace EngineRender {

struct PS4DirectMemoryBlock {
    off_t offset = 0;
    size_t size = 0;
    void* mappedAddress = nullptr;
    int memoryType = 0; // 3 = Garlic (WC), 0 = Onion (WB)
};

class PS4Device {
public:
    PS4Device();
    ~PS4Device();

    bool Initialize(int width, int height);
    void Shutdown();

    // Direct memory allocations (Garlic / Onion)
    bool AllocateDirectMemory(size_t size, size_t alignment, int memoryType, PS4DirectMemoryBlock& outBlock);
    void FreeDirectMemory(PS4DirectMemoryBlock& block);

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

    float* GetDepthBuffer() { return m_depthBuffer.data(); }
    const float* GetDepthBuffer() const { return m_depthBuffer.data(); }
    void ClearDepthBuffer(float clearVal = 1.0f);

private:
    int m_width = 1920;
    int m_height = 1080;
    bool m_initialized = false;

    // Depth buffer for 3D scene rendering (1920x1080 float32)
    std::vector<float> m_depthBuffer;

    std::vector<PS4DirectMemoryBlock> m_allocatedBlocks;
};

} // namespace EngineRender
