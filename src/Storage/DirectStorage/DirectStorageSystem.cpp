#include "DirectStorageSystem.h"
#include "Storage/ContentIO.h"
#include "Storage/StreamingTexture.h"
#include <fstream>

#if defined(_WIN32)
#include <iostream>
#include <sstream>
#include <filesystem>

#define DSTORAGE_LOG(msg) std::cout << "[DirectStorage] " << msg << std::endl
#define DSTORAGE_ERR(msg) std::cerr << "[DirectStorage ERROR] " << msg << std::endl

namespace EngineStorage {

DirectStorageSystem::DirectStorageSystem() {
}

DirectStorageSystem::~DirectStorageSystem() {
    Shutdown();
}

bool DirectStorageSystem::Initialize(ID3D12Device* device) {
    Shutdown();
    if (!device) {
        DSTORAGE_ERR("Invalid D3D12 device provided for DirectStorage initialization.");
        return false;
    }

    // The redistributable wrapper does not reliably resolve its core from an
    // AddDllDirectory subdirectory. Load the exact packaged core first.
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    const auto corePath = std::filesystem::path(executable).parent_path() /
        L"ThirdParty" / L"Microsoft" / L"DirectStorage" / L"dstoragecore.dll";
    m_coreModule = LoadLibraryExW(corePath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!m_coreModule) {
        m_statusSummary = "DirectStorage: core runtime unavailable (error " + std::to_string(GetLastError()) + ")";
        DSTORAGE_ERR(m_statusSummary);
        return false;
    }
    // Configure DirectStorage runtime
    DSTORAGE_CONFIGURATION config = {};
    config.NumSubmitThreads = 0; // System default
    config.NumBuiltInCpuDecompressionThreads = 0; // System default
    config.ForceMappingLayer = FALSE;
    config.DisableBypassIO = FALSE;
    config.DisableTelemetry = FALSE;
    config.DisableGpuDecompressionMetacommand = FALSE;
    config.DisableGpuDecompression = FALSE;

    HRESULT hr = DStorageSetConfiguration(&config);
    if (FAILED(hr) && hr != E_DSTORAGE_STAGING_BUFFER_LOCKED) {
        DSTORAGE_ERR("DStorageSetConfiguration failed. HRESULT = 0x" << std::hex << hr);
    }

    // Retrieve global DirectStorage Factory
    hr = DStorageGetFactory(IID_PPV_ARGS(&m_factory));
    if (FAILED(hr) || !m_factory) {
        DSTORAGE_ERR("Failed to get DirectStorage factory. HRESULT = 0x" << std::hex << hr);
        m_supported = false;
        m_statusSummary = "DirectStorage: Unavailable (Failed to load factory)";
        return false;
    }

    // Configure factory staging buffer (32 MB default buffer for high-bandwidth chunking)
    m_factory->SetStagingBufferSize(DSTORAGE_STAGING_BUFFER_SIZE_32MB);

    // Create File Request Queue
    DSTORAGE_QUEUE_DESC fileQueueDesc = {};
    fileQueueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    fileQueueDesc.Capacity = 256;
    fileQueueDesc.Priority = DSTORAGE_PRIORITY_NORMAL;
    fileQueueDesc.Name = "MyEngine File Queue";
    fileQueueDesc.Device = device;

    hr = m_factory->CreateQueue(&fileQueueDesc, IID_PPV_ARGS(&m_fileQueue));
    if (FAILED(hr) || !m_fileQueue) {
        DSTORAGE_ERR("Failed to create DirectStorage File Queue. HRESULT = 0x" << std::hex << hr);
        Shutdown();
        return false;
    }

    // Create In-Memory Request Queue
    DSTORAGE_QUEUE_DESC memQueueDesc = {};
    memQueueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
    memQueueDesc.Capacity = 256;
    memQueueDesc.Priority = DSTORAGE_PRIORITY_NORMAL;
    memQueueDesc.Name = "MyEngine Memory Queue";
    memQueueDesc.Device = device;

    hr = m_factory->CreateQueue(&memQueueDesc, IID_PPV_ARGS(&m_memoryQueue));
    if (FAILED(hr) || !m_memoryQueue) {
        DSTORAGE_ERR("Failed to create DirectStorage Memory Queue. HRESULT = 0x" << std::hex << hr);
        Shutdown();
        return false;
    }

    hr=device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&m_readFence));
    m_readEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(FAILED(hr)||!m_readEvent){Shutdown();return false;}
    m_supported = true;
    m_device=device;
    RegisterContentReader(this,[](void* owner,const std::filesystem::path& file,uint64_t offset,uint32_t size,std::vector<uint8_t>& output){
        return static_cast<DirectStorageSystem*>(owner)->ReadRange(file,offset,size,output);
    });
    RegisterStreamingTextureReader([](void* owner,const std::filesystem::path& file,ID3D12Resource** output,uint32_t& width,uint32_t& height,uint32_t& mips){return static_cast<DirectStorageSystem*>(owner)->ReadTexture(file,output,width,height,mips);});

    // Check compression capabilities on the file queue
    ComPtr<IDStorageQueue2> queue2;
    if (SUCCEEDED(m_fileQueue.As(&queue2))) {
        DSTORAGE_COMPRESSION_SUPPORT support = queue2->GetCompressionSupport(DSTORAGE_COMPRESSION_FORMAT_GDEFLATE);
        if (support & (DSTORAGE_COMPRESSION_SUPPORT_GPU_OPTIMIZED | DSTORAGE_COMPRESSION_SUPPORT_GPU_FALLBACK)) {
            m_gpuDecompressionSupported = true;
        }
    }

    // Queue creation does not prove BypassIO support for a particular file/drive.
    m_bypassIOSupported = false;

    std::ostringstream oss;
    oss << "DirectStorage: Active (GPU Decomp: "
        << (m_gpuDecompressionSupported ? "GDeflate Enabled" : "CPU Fallback")
        << ", BypassIO: not verified; asset streaming connected)";
    m_statusSummary = oss.str();

    DSTORAGE_LOG("Initialized successfully: " << m_statusSummary);
    return true;
}

void DirectStorageSystem::Shutdown() {
    UnregisterContentReader(this); // Wait for any asset worker read before releasing queues.
    if(m_fileQueue)m_fileQueue->Close();
    if(m_memoryQueue)m_memoryQueue->Close();
    m_fileQueue.Reset();
    m_memoryQueue.Reset();
    m_factory.Reset();
    m_device.Reset();
    m_readFence.Reset();
    if(m_readEvent){CloseHandle(m_readEvent);m_readEvent=nullptr;}
    m_readSequence=0;
    if (m_coreModule) { FreeLibrary(m_coreModule); m_coreModule = nullptr; }
    m_supported = false;
    m_gpuDecompressionSupported = false;
    m_bypassIOSupported = false;
    m_statusSummary = "DirectStorage: Offline";
}

bool DirectStorageSystem::ReadRange(const std::filesystem::path& path,uint64_t offset,uint32_t bytes,std::vector<uint8_t>& output) {
    if(!m_supported||!m_fileQueue||!m_readFence||!m_readEvent)return false;
    ComPtr<IDStorageFile> file;
    if(FAILED(OpenFile(std::filesystem::absolute(path).wstring(),file)))return false;
    BY_HANDLE_FILE_INFORMATION info{};
    if(FAILED(file->GetFileInformation(&info)))return false;
    const uint64_t length=(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
    if(offset>length)return false;
    const uint64_t count=bytes?bytes:length-offset;
    if(count>512ull*1024*1024||offset+count>length)return false;
    output.resize(static_cast<size_t>(count));
    if(!count)return true;
    const auto value=++m_readSequence;
    if(FAILED(m_readFence->SetEventOnCompletion(value,m_readEvent)))return false;
    if(FAILED(EnqueueFileToMemory(file.Get(),offset,static_cast<uint32_t>(count),output.data())))return false;
    m_fileQueue->EnqueueSignal(m_readFence.Get(),value);
    m_fileQueue->Submit();
    auto wait=WaitForSingleObject(m_readEvent,10000);
    if(wait!=WAIT_OBJECT_0){
        // Close guarantees the queue will no longer access the destination.
        m_fileQueue->Close();m_supported=false;
        m_statusSummary="DirectStorage: read timed out; standard I/O fallback";
        output.clear();return false;
    }
    DSTORAGE_ERROR_RECORD error{};m_fileQueue->RetrieveErrorRecord(&error);
    if(FAILED(error.FirstFailure.HResult)){output.clear();return false;}
    ++directStorageReads;directStorageBytes.fetch_add(count);
    return true;
}

bool DirectStorageSystem::ReadTexture(const std::filesystem::path& path,ID3D12Resource** output,uint32_t& width,uint32_t& height,uint32_t& mips){
    if(!output||!m_supported||!m_device||!m_fileQueue)return false;
    *output=nullptr;
    std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input)return false;
    const auto length=input.tellg();input.seekg(0);StreamingTextureHeader header{};input.read(reinterpret_cast<char*>(&header),sizeof(header));
    if(!input||header.magic!=0x58545347||header.version!=1||header.format!=28||!header.width||!header.height||header.width>16384||header.height>16384||!header.mips||header.mips>15||!header.compressedBytes||uint64_t(header.compressedBytes)+sizeof(header)!=uint64_t(length)||header.rawBytes>256u*1024*1024)return false;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=header.width;desc.Height=header.height;desc.DepthOrArraySize=1;desc.MipLevels=UINT16(header.mips);desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    UINT64 expected=0;m_device->GetCopyableFootprints(&desc,0,header.mips,0,nullptr,nullptr,nullptr,&expected);
    if(expected!=header.rawBytes)return false;
    ComPtr<IDStorageFile> file;if(FAILED(OpenFile(std::filesystem::absolute(path).wstring(),file)))return false;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> texture;
    if(FAILED(m_device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&texture))))return false;
    DSTORAGE_REQUEST request{};request.Options.SourceType=DSTORAGE_REQUEST_SOURCE_FILE;request.Options.DestinationType=DSTORAGE_REQUEST_DESTINATION_MULTIPLE_SUBRESOURCES;request.Options.CompressionFormat=DSTORAGE_COMPRESSION_FORMAT_GDEFLATE;
    request.Source.File={file.Get(),sizeof(header),header.compressedBytes};request.UncompressedSize=header.rawBytes;request.Destination.MultipleSubresources={texture.Get(),0};request.Name="Cooked GDeflate texture";
    const auto value=++m_readSequence;if(FAILED(m_readFence->SetEventOnCompletion(value,m_readEvent)))return false;
    m_fileQueue->EnqueueRequest(&request);m_fileQueue->EnqueueSignal(m_readFence.Get(),value);m_fileQueue->Submit();
    if(WaitForSingleObject(m_readEvent,10000)!=WAIT_OBJECT_0){m_fileQueue->Close();m_supported=false;m_statusSummary="DirectStorage: texture timeout; standard texture fallback";return false;}
    DSTORAGE_ERROR_RECORD error{};m_fileQueue->RetrieveErrorRecord(&error);
    if(FAILED(error.FirstFailure.HResult)){DSTORAGE_ERR("Texture request failed: 0x"<<std::hex<<error.FirstFailure.HResult);return false;}
    width=header.width;height=header.height;mips=header.mips;*output=texture.Detach();
    ++streamingTextureReads;gdeflateDecodedBytes+=header.rawBytes;
    DSTORAGE_LOG("GDeflate texture streamed: "<<path.string()<<" ("<<header.rawBytes<<" decoded bytes)");return true;
}

HRESULT DirectStorageSystem::OpenFile(const std::wstring& path, ComPtr<IDStorageFile>& outFile) {
    if (!m_factory) return E_POINTER;
    return m_factory->OpenFile(path.c_str(), IID_PPV_ARGS(&outFile));
}

HRESULT DirectStorageSystem::CreateStatusArray(UINT32 capacity, ComPtr<IDStorageStatusArray>& outArray, const char* name) {
    if (!m_factory) return E_POINTER;
    return m_factory->CreateStatusArray(capacity, name, IID_PPV_ARGS(&outArray));
}

HRESULT DirectStorageSystem::EnqueueFileToBuffer(IDStorageFile* file,
                                                UINT64 fileOffset,
                                                UINT32 size,
                                                ID3D12Resource* buffer,
                                                UINT64 bufferOffset,
                                                DSTORAGE_COMPRESSION_FORMAT compression,
                                                UINT32 uncompressedSize) {
    if (!m_fileQueue || !file || !buffer) return E_INVALIDARG;

    DSTORAGE_REQUEST request = {};
    request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_BUFFER;
    request.Options.CompressionFormat = compression;
    request.Source.File.Source = file;
    request.Source.File.Offset = fileOffset;
    request.Source.File.Size = size;
    request.Destination.Buffer.Resource = buffer;
    request.Destination.Buffer.Offset = bufferOffset;
    request.Destination.Buffer.Size = (uncompressedSize > 0) ? uncompressedSize : size;
    request.UncompressedSize = (uncompressedSize > 0) ? uncompressedSize : size;

    m_fileQueue->EnqueueRequest(&request);
    return S_OK;
}

HRESULT DirectStorageSystem::EnqueueFileToMemory(IDStorageFile* file,
                                                UINT64 fileOffset,
                                                UINT32 size,
                                                void* targetMemory,
                                                DSTORAGE_COMPRESSION_FORMAT compression,
                                                UINT32 uncompressedSize) {
    if (!m_fileQueue || !file || !targetMemory) return E_INVALIDARG;

    DSTORAGE_REQUEST request = {};
    request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MEMORY;
    request.Options.CompressionFormat = compression;
    request.Source.File.Source = file;
    request.Source.File.Offset = fileOffset;
    request.Source.File.Size = size;
    request.Destination.Memory.Buffer = targetMemory;
    request.Destination.Memory.Size = (uncompressedSize > 0) ? uncompressedSize : size;
    request.UncompressedSize = (uncompressedSize > 0) ? uncompressedSize : size;

    m_fileQueue->EnqueueRequest(&request);
    return S_OK;
}

HRESULT DirectStorageSystem::EnqueueMemoryToBuffer(const void* sourceMemory,
                                                  UINT32 sourceSize,
                                                  ID3D12Resource* buffer,
                                                  UINT64 bufferOffset,
                                                  DSTORAGE_COMPRESSION_FORMAT compression,
                                                  UINT32 uncompressedSize) {
    if (!m_memoryQueue || !sourceMemory || !buffer) return E_INVALIDARG;

    DSTORAGE_REQUEST request = {};
    request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
    request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_BUFFER;
    request.Options.CompressionFormat = compression;
    request.Source.Memory.Source = sourceMemory;
    request.Source.Memory.Size = sourceSize;
    request.Destination.Buffer.Resource = buffer;
    request.Destination.Buffer.Offset = bufferOffset;
    request.Destination.Buffer.Size = (uncompressedSize > 0) ? uncompressedSize : sourceSize;
    request.UncompressedSize = (uncompressedSize > 0) ? uncompressedSize : sourceSize;

    m_memoryQueue->EnqueueRequest(&request);
    return S_OK;
}

void DirectStorageSystem::Submit() {
    if (m_fileQueue) {
        m_fileQueue->Submit();
    }
    if (m_memoryQueue) {
        m_memoryQueue->Submit();
    }
}

const char* DirectStorageSystem::CompressionFormatToString(DSTORAGE_COMPRESSION_FORMAT format) {
    switch (format) {
        case DSTORAGE_COMPRESSION_FORMAT_NONE: return "None (Uncompressed)";
        case DSTORAGE_COMPRESSION_FORMAT_GDEFLATE: return "GDeflate (GPU Accelerated)";
        default: return "Custom / Application defined";
    }
}

} // namespace EngineStorage
#endif // _WIN32
