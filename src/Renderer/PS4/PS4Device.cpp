#include "PS4Device.h"
#include <iostream>
#include <cstring>
#include <cstdlib>

namespace EngineRender {

PS4Device::PS4Device() = default;

PS4Device::~PS4Device() {
    Shutdown();
}

bool PS4Device::Initialize(int width, int height) {
    m_width = width > 0 ? width : 1920;
    m_height = height > 0 ? height : 1080;

    // Allocate 32-bit floating point depth buffer
    m_depthBuffer.resize(static_cast<size_t>(m_width) * m_height, 1.0f);

    m_initialized = true;
    std::cout << "[PS4Device] Initialized device with resolution " << m_width << "x" << m_height << std::endl;
    return true;
}

void PS4Device::Shutdown() {
    for (auto& block : m_allocatedBlocks) {
        FreeDirectMemory(block);
    }
    m_allocatedBlocks.clear();
    m_depthBuffer.clear();
    m_initialized = false;
}

bool PS4Device::AllocateDirectMemory(size_t size, size_t alignment, int memoryType, PS4DirectMemoryBlock& outBlock) {
    if (alignment == 0) alignment = 0x200000;
    size_t alignedSize = (size + alignment - 1) / alignment * alignment;

#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    off_t offset = 0;
    int rc = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), alignedSize, alignment, memoryType, &offset);
    if (rc < 0) {
        std::cerr << "[PS4Device] sceKernelAllocateDirectMemory failed: " << rc << std::endl;
        return false;
    }

    void* mapped = nullptr;
    rc = sceKernelMapDirectMemory(&mapped, alignedSize, 0x33, 0, offset, alignment);
    if (rc < 0) {
        std::cerr << "[PS4Device] sceKernelMapDirectMemory failed: " << rc << std::endl;
        sceKernelReleaseDirectMemory(offset, alignedSize);
        return false;
    }

    outBlock.offset = offset;
    outBlock.size = alignedSize;
    outBlock.mappedAddress = mapped;
    outBlock.memoryType = memoryType;
#else
    outBlock.offset = 0;
    outBlock.size = alignedSize;
    outBlock.mappedAddress = malloc(alignedSize);
    outBlock.memoryType = memoryType;
    if (!outBlock.mappedAddress) return false;
    memset(outBlock.mappedAddress, 0, alignedSize);
#endif

    m_allocatedBlocks.push_back(outBlock);
    return true;
}

void PS4Device::FreeDirectMemory(PS4DirectMemoryBlock& block) {
    if (!block.mappedAddress) return;

#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    sceKernelReleaseDirectMemory(block.offset, block.size);
#else
    free(block.mappedAddress);
#endif

    block.mappedAddress = nullptr;
    block.offset = 0;
    block.size = 0;
}

void PS4Device::ClearDepthBuffer(float clearVal) {
    std::fill(m_depthBuffer.begin(), m_depthBuffer.end(), clearVal);
}

} // namespace EngineRender
