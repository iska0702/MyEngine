#pragma once
#include <string>
#include <atomic>
#include <cstdint>

#if defined(_WIN32)
#include <d3d12.h>
#include <wrl.h>
#endif

enum class TextureState {
    Unloaded,
    Loading,
    Uploading,
    Uploaded, // Upload copy has been scheduled or finished, waiting for sync and barrier
    Ready,    // Barrier executed, ready to use
    Error
};

class Texture {
public:
    Texture(const std::string& filepath);
    ~Texture();

    const std::string& GetFilepath() const { return m_filepath; }
    TextureState GetState() const { return m_state.load(); }
    void SetState(TextureState state) { m_state.store(state); }

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    void SetDimensions(int w, int h) { m_width = w; m_height = h; }

    int GetNumChannels() const { return m_numChannels; }
    void SetNumChannels(int n) { m_numChannels = n; }

    uint8_t* GetCpuData() const { return m_cpuData; }
    void SetCpuData(uint8_t* data) { m_cpuData = data; }

#if defined(_WIN32)
    ID3D12Resource* GetResource() const { return m_resource.Get(); }
    void SetResource(Microsoft::WRL::ComPtr<ID3D12Resource> res) { m_resource = res; }

    ID3D12Resource* GetUploadHeap() const { return m_uploadHeap.Get(); }
    void SetUploadHeap(Microsoft::WRL::ComPtr<ID3D12Resource> res) { m_uploadHeap = res; }
#endif

    void* GetMetalTexture() const { return m_metalTexture; }
    void SetMetalTexture(void* tex) { m_metalTexture = tex; }

    uint32_t GetSrvIndex() const { return m_srvIndex; }
    void SetSrvIndex(uint32_t index) { m_srvIndex = index; }

    uint64_t GetFenceValue() const { return m_fenceValue; }
    void SetFenceValue(uint64_t v) { m_fenceValue = v; }

    void FreeCpuData();

private:
    std::string m_filepath;
    std::atomic<TextureState> m_state;
    
    int m_width;
    int m_height;
    int m_numChannels;
    uint8_t* m_cpuData;

#if defined(_WIN32)
    Microsoft::WRL::ComPtr<ID3D12Resource> m_resource;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_uploadHeap;
#endif
    void* m_metalTexture = nullptr;

    uint32_t m_srvIndex = 0;
    uint64_t m_fenceValue = 0;
};
