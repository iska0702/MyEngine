#define NOMINMAX
#include "TextureStreamer.h"
#include "Renderer.h"
#include "Storage/ContentFile.h"
#include "Platform/PlatformUtils.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "stb_image_resize2.h"
#include <fstream>

#if defined(_WIN32)
TextureStreamer::TextureStreamer(Renderer* renderer, ID3D12Device* device)
    : m_renderer(renderer), m_device(device), m_fenceValue(0), m_isRunning(false) {
}

TextureStreamer::~TextureStreamer() {
    Shutdown();
}

bool TextureStreamer::Initialize() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;

    if (FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_copyCommandQueue)))) return false;
    m_copyCommandQueue->SetName(L"TextureStreamerCopyQueue");
    if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&m_copyCommandAllocator)))) return false;
    if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, m_copyCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_copyCommandList)))) return false;
    m_copyCommandList->SetName(L"TextureStreamerCopyCommandList");
    m_copyCommandList->Close(); // close before using for recording

    if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_copyFence)))) return false;
    m_copyFence->SetName(L"TextureStreamerCopyFence");

    m_isRunning = true;
    m_workerThread = std::thread(&TextureStreamer::WorkerThread, this);

    return true;
}

void TextureStreamer::Shutdown() {
    if (m_isRunning) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_isRunning = false;
        }
        m_cv.notify_one();
        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }
    }
}

std::shared_ptr<Texture> TextureStreamer::LoadTexture(const std::string& filepath) {
    auto texture = std::make_shared<Texture>(filepath);
    texture->SetState(TextureState::Loading);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_loadQueue.push(texture);
    }
    m_cv.notify_one();

    return texture;
}

std::vector<std::shared_ptr<Texture>> TextureStreamer::GetNewlyUploadedTextures() {
    std::vector<std::shared_ptr<Texture>> result;
    std::lock_guard<std::mutex> lock(m_mutex);
    result.swap(m_uploadedQueue);
    return result;
}

void TextureStreamer::Update() {
    UINT64 completed = m_copyFence->GetCompletedValue();
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_cleanupQueue.begin(); it != m_cleanupQueue.end(); ) {
        if (completed >= (*it)->GetFenceValue()) {
            (*it)->SetUploadHeap(nullptr);
            it = m_cleanupQueue.erase(it);
        } else {
            ++it;
        }
    }
}

void TextureStreamer::WorkerThread() {
    while (true) {
        std::shared_ptr<Texture> texture;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() { return !m_isRunning || !m_loadQueue.empty(); });

            if (!m_isRunning && m_loadQueue.empty()) break;

            texture = m_loadQueue.front();
            m_loadQueue.pop();
        }

        // 1. Decode Image off main thread
        int width, height, channels;
        stbi_set_flip_vertically_on_load(false);
        
        uint8_t* data = nullptr;
        std::vector<uint8_t> encoded;
        std::string assetPath=texture->GetFilepath();
        std::replace(assetPath.begin(),assetPath.end(),'\\','/');
        bool loaded=false;
        // Cooked textures contain the complete GPU-ready mip chain. Keep PNG/JPG
        // decoding as the fallback for imported or uncooked assets.
        std::filesystem::path cooked;
        if(assetPath.rfind("Content/",0)==0){
            wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
            cooked=std::filesystem::path(executable).parent_path()/L"Contents/Streaming"/std::filesystem::u8path(assetPath.substr(8)+".gdst");
        }else cooked=std::filesystem::u8path(assetPath+".gdst");
        uint32_t cookedWidth=0,cookedHeight=0,cookedMips=0;ID3D12Resource* cookedResource=nullptr;
        bool current=true;std::error_code cookedError;
        auto original=std::filesystem::u8path(assetPath);
        if(assetPath.rfind("Content/",0)==0)original=std::filesystem::path(EnginePlatform::PlatformUtils::GetContentRoot())/std::filesystem::u8path(assetPath.substr(8));
        if(std::filesystem::is_regular_file(original,cookedError)){
            auto cookedContainer=cooked;
            if(assetPath.rfind("Content/",0)==0&&!std::filesystem::is_regular_file(cooked,cookedError)){
                wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);cookedContainer=std::filesystem::path(executable).parent_path()/L"Contents/content10.pak";
            }
            if(std::filesystem::is_regular_file(cookedContainer,cookedError))current=std::filesystem::last_write_time(cookedContainer,cookedError)>=std::filesystem::last_write_time(original,cookedError);
        }
        if(current&&EngineStorage::TryReadStreamingTexture(cooked,&cookedResource,cookedWidth,cookedHeight,cookedMips)){
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;resource.Attach(cookedResource);texture->SetResource(resource);
            texture->SetDimensions(cookedWidth,cookedHeight);texture->SetNumChannels(4);texture->SetFenceValue(0);texture->SetState(TextureState::Uploading);
            std::lock_guard<std::mutex> lock(m_mutex);m_uploadedQueue.push_back(texture);continue;
        }
        if(assetPath.rfind("Content/",0)==0) {
            auto loose=std::filesystem::path(EnginePlatform::PlatformUtils::GetContentRoot())/std::filesystem::u8path(assetPath.substr(8));
            std::error_code fileError;
            if(std::filesystem::is_regular_file(loose,fileError)) {
                auto path8=loose.u8string();
                loaded=EngineStorage::ReadContentBytes(std::string(path8.begin(),path8.end()),encoded);
            }
        }
        if(!loaded) loaded=EngineStorage::ReadContentBytes(texture->GetFilepath(),encoded);
        if(loaded && !encoded.empty() && encoded.size()<static_cast<size_t>(INT_MAX))
            data=stbi_load_from_memory(encoded.data(),static_cast<int>(encoded.size()),&width,&height,&channels,4);
        if (!data) {
            texture->SetState(TextureState::Error);
            continue;
        }

        int mipLevels = 1;
        // Keep generating mips down to 1x1
        int w = width, h = height;
        while (w > 1 || h > 1) {
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
            mipLevels++;
        }

        texture->SetDimensions(width, height);
        texture->SetNumChannels(4);
        texture->SetCpuData(data); 

        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Alignment = 0;
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = mipLevels;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES heapPropsDefault = {};
        heapPropsDefault.Type = D3D12_HEAP_TYPE_DEFAULT;

        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        if (FAILED(m_device->CreateCommittedResource(
            &heapPropsDefault, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)))) {
            texture->SetState(TextureState::Error);
            texture->FreeCpuData();
            continue;
        }

        if (resource) {
            std::string path = texture->GetFilepath();
            size_t lastSlash = path.find_last_of("\\/");
            std::string filename = (lastSlash == std::string::npos) ? path : path.substr(lastSlash + 1);
            std::wstring wfilename(filename.begin(), filename.end());
            resource->SetName(wfilename.c_str());
        }

        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(mipLevels);
        std::vector<UINT> numRows(mipLevels);
        std::vector<UINT64> rowSizeInBytes(mipLevels);
        UINT64 totalBytes = 0;
        m_device->GetCopyableFootprints(&texDesc, 0, mipLevels, 0, footprints.data(), numRows.data(), rowSizeInBytes.data(), &totalBytes);

        D3D12_RESOURCE_DESC uploadDesc = {};
        uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        uploadDesc.Width = totalBytes;
        uploadDesc.Height = 1;
        uploadDesc.DepthOrArraySize = 1;
        uploadDesc.MipLevels = 1;
        uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
        uploadDesc.SampleDesc.Count = 1;
        uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES heapPropsUpload = {};
        heapPropsUpload.Type = D3D12_HEAP_TYPE_UPLOAD;

        Microsoft::WRL::ComPtr<ID3D12Resource> uploadHeap;
        if (FAILED(m_device->CreateCommittedResource(
            &heapPropsUpload, D3D12_HEAP_FLAG_NONE, &uploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadHeap)))) {
            texture->SetState(TextureState::Error);
            texture->FreeCpuData();
            continue;
        }
        if (uploadHeap) uploadHeap->SetName(L"TextureUploadHeap");

        UINT8* pData;
        if (SUCCEEDED(uploadHeap->Map(0, nullptr, reinterpret_cast<void**>(&pData)))) {
            uint8_t* prevMipData = data;
            int prevW = width;
            int prevH = height;
            std::vector<uint8_t*> mipDatas; // to free later

            for (int i = 0; i < mipLevels; ++i) {
                int mipW = std::max(1, width >> i);
                int mipH = std::max(1, height >> i);
                
                uint8_t* currentMipData = nullptr;
                if (i == 0) {
                    currentMipData = data;
                } else {
                    currentMipData = new uint8_t[mipW * mipH * 4];
                    stbir_resize_uint8_srgb(prevMipData, prevW, prevH, 0,
                                            currentMipData, mipW, mipH, 0,
                                            (stbir_pixel_layout)4);
                    mipDatas.push_back(currentMipData);
                }

                auto& fp = footprints[i];
                for (UINT y = 0; y < mipH; ++y) {
                    memcpy(pData + fp.Offset + y * fp.Footprint.RowPitch,
                           currentMipData + y * mipW * 4,
                           mipW * 4);
                }
                
                prevMipData = currentMipData;
                prevW = mipW;
                prevH = mipH;
            }
            uploadHeap->Unmap(0, nullptr);
            for (auto* m : mipDatas) delete[] m;
        }

        texture->SetResource(resource);
        texture->SetUploadHeap(uploadHeap);
        texture->FreeCpuData();

        // 3. Command list recording
        std::lock_guard<std::mutex> lockList(m_mutex); 

        // Ensure GPU has finished prior execution before resetting allocator & command list
        if (m_fenceValue > 0 && m_copyFence && m_copyFence->GetCompletedValue() < m_fenceValue) {
            HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
            if (eventHandle) {
                if (SUCCEEDED(m_copyFence->SetEventOnCompletion(m_fenceValue, eventHandle))) {
                    WaitForSingleObject(eventHandle, INFINITE);
                }
                CloseHandle(eventHandle);
            }
        }

        m_copyCommandAllocator->Reset();
        m_copyCommandList->Reset(m_copyCommandAllocator.Get(), nullptr);

        // D3D12 Copy Queues use implicit state promotion from COMMON to COPY_DEST, 
        // so we don't need (and shouldn't use) an explicit resource barrier here.

        for (int i = 0; i < mipLevels; ++i) {
            D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
            dstLoc.pResource = resource.Get();
            dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dstLoc.SubresourceIndex = i;

            D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
            srcLoc.pResource = uploadHeap.Get();
            srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            srcLoc.PlacedFootprint = footprints[i];

            m_copyCommandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
        }
        
        m_copyCommandList->Close();
        
        ID3D12CommandList* ppCommandLists[] = { m_copyCommandList.Get() };
        m_copyCommandQueue->ExecuteCommandLists(1, ppCommandLists);
        
        m_fenceValue++;
        texture->SetFenceValue(m_fenceValue);
        m_copyCommandQueue->Signal(m_copyFence.Get(), m_fenceValue);

        texture->SetState(TextureState::Uploading);
        
        m_cleanupQueue.push_back(texture);
        m_uploadedQueue.push_back(texture);
    }
}
#else
TextureStreamer::TextureStreamer(Renderer* renderer, ID3D12Device* device)
    : m_renderer(renderer), m_device(device), m_fenceValue(0), m_isRunning(false) {}

TextureStreamer::~TextureStreamer() { Shutdown(); }
bool TextureStreamer::Initialize() { return true; }
void TextureStreamer::Shutdown() {}
std::shared_ptr<Texture> TextureStreamer::LoadTexture(const std::string& filepath) {
    auto texture = std::make_shared<Texture>(filepath);
    texture->SetState(TextureState::Ready);
    return texture;
}
void TextureStreamer::Update() {}
std::vector<std::shared_ptr<Texture>> TextureStreamer::GetNewlyUploadedTextures() { return {}; }
void TextureStreamer::WorkerThread() {}
#endif
