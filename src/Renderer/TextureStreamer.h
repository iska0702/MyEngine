#pragma once
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <memory>
#include "Texture.h"

#if defined(_WIN32)
#include <d3d12.h>
#include <wrl.h>
#else
#include "Platform/PlatformTypes.h"
#endif

class Renderer;

class TextureStreamer {
public:
    TextureStreamer(Renderer* renderer, ID3D12Device* device = nullptr);
    ~TextureStreamer();

    bool Initialize();
    void Shutdown();

    // Request a texture to be loaded streaming
    std::shared_ptr<Texture> LoadTexture(const std::string& filepath);

    // Call this once per frame from Renderer main thread to cleanup upload heaps
    void Update();

#if defined(_WIN32)
    ID3D12CommandQueue* GetCopyQueue() const { return m_copyCommandQueue.Get(); }
    ID3D12Fence* GetFence() const { return m_copyFence.Get(); }
#endif

    // Textures that have finished uploading and waiting for SRV and barrier
    std::vector<std::shared_ptr<Texture>> GetNewlyUploadedTextures();

private:
    void WorkerThread();

    Renderer* m_renderer;
#if defined(_WIN32)
    ID3D12Device* m_device;

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_copyCommandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_copyCommandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_copyCommandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_copyFence;
#else
    void* m_device = nullptr;
#endif
    uint64_t m_fenceValue = 0;

    std::thread m_workerThread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_isRunning = false;

    std::queue<std::shared_ptr<Texture>> m_loadQueue;
    std::vector<std::shared_ptr<Texture>> m_uploadedQueue; // Ready for main thread
    std::vector<std::shared_ptr<Texture>> m_cleanupQueue; // Keep alive until fence passed
};
