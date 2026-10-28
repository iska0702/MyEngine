#pragma once

#if defined(_WIN32)
#include <d3d12.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include "dstorage.h"

namespace EngineStorage {

using Microsoft::WRL::ComPtr;

/**
 * @brief Microsoft DirectStorage (dstorage.dll) runtime manager for MyEngine.
 * Handles fast asynchronous file-to-GPU/CPU streaming, GPU decompression (GDeflate),
 * queue management, and hardware BypassIO acceleration.
 */
class DirectStorageSystem {
public:
    DirectStorageSystem();
    ~DirectStorageSystem();

    /**
     * @brief Initializes the DirectStorage factory and queues.
     * @param device D3D12 device to bind with DirectStorage GPU decompression.
     * @return true if DirectStorage was successfully initialized.
     */
    bool Initialize(ID3D12Device* device);

    /**
     * @brief Shuts down all queues and releases the DirectStorage factory.
     */
    void Shutdown();

    // Query status
    bool IsSupported() const { return m_supported; }
    bool HasGpuDecompression() const { return m_gpuDecompressionSupported; }
    bool HasBypassIO() const { return m_bypassIOSupported; }
    const std::string& GetStatusSummary() const { return m_statusSummary; }

    // Direct access to interfaces
    IDStorageFactory* GetFactory() const { return m_factory.Get(); }
    IDStorageQueue* GetFileQueue() const { return m_fileQueue.Get(); }
    IDStorageQueue* GetMemoryQueue() const { return m_memoryQueue.Get(); }

    /**
     * @brief Opens a file handle for asynchronous DirectStorage access.
     * @param path Full or relative filesystem path.
     * @param outFile Output COM pointer to the opened IDStorageFile.
     * @return HRESULT S_OK on success.
     */
    HRESULT OpenFile(const std::wstring& path, ComPtr<IDStorageFile>& outFile);

    /**
     * @brief Creates a status tracking array for monitoring request completion.
     */
    HRESULT CreateStatusArray(UINT32 capacity, ComPtr<IDStorageStatusArray>& outArray, const char* name = nullptr);

    /**
     * @brief Enqueues a read request from an opened file directly into a D3D12 Buffer resource.
     */
    HRESULT EnqueueFileToBuffer(IDStorageFile* file,
                                UINT64 fileOffset,
                                UINT32 size,
                                ID3D12Resource* buffer,
                                UINT64 bufferOffset,
                                DSTORAGE_COMPRESSION_FORMAT compression = DSTORAGE_COMPRESSION_FORMAT_NONE,
                                UINT32 uncompressedSize = 0);

    /**
     * @brief Enqueues a read request from an opened file into CPU system memory.
     */
    HRESULT EnqueueFileToMemory(IDStorageFile* file,
                                UINT64 fileOffset,
                                UINT32 size,
                                void* targetMemory,
                                DSTORAGE_COMPRESSION_FORMAT compression = DSTORAGE_COMPRESSION_FORMAT_NONE,
                                UINT32 uncompressedSize = 0);

    /**
     * @brief Enqueues an in-memory block for decompression and upload into a D3D12 Buffer resource.
     */
    HRESULT EnqueueMemoryToBuffer(const void* sourceMemory,
                                  UINT32 sourceSize,
                                  ID3D12Resource* buffer,
                                  UINT64 bufferOffset,
                                  DSTORAGE_COMPRESSION_FORMAT compression = DSTORAGE_COMPRESSION_FORMAT_NONE,
                                  UINT32 uncompressedSize = 0);

    /**
     * @brief Submits all pending requests in file and memory queues to the DirectStorage driver.
     */
    void Submit();
    // Runs on the asset worker; completion protects the caller's destination memory.
    bool ReadRange(const std::filesystem::path& file,uint64_t offset,uint32_t bytes,std::vector<uint8_t>& output);
    bool ReadTexture(const std::filesystem::path& file,ID3D12Resource** output,uint32_t& width,uint32_t& height,uint32_t& mips);

    // Utilities
    static const char* CompressionFormatToString(DSTORAGE_COMPRESSION_FORMAT format);

private:
    HMODULE m_coreModule = nullptr;
    ComPtr<ID3D12Device> m_device;
    ComPtr<IDStorageFactory> m_factory;
    ComPtr<IDStorageQueue>   m_fileQueue;
    ComPtr<IDStorageQueue>   m_memoryQueue;
    ComPtr<ID3D12Fence> m_readFence;
    HANDLE m_readEvent=nullptr;
    uint64_t m_readSequence=0;

    bool m_supported = false;
    bool m_gpuDecompressionSupported = false;
    bool m_bypassIOSupported = false;
    std::string m_statusSummary;
};

} // namespace EngineStorage
#endif // _WIN32
