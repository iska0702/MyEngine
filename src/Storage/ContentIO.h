#pragma once
#include <filesystem>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>
#if defined(_WIN32)
struct ID3D12Resource;
#endif
namespace EngineStorage {
using ContentReadFunction=bool(*)(void*,const std::filesystem::path&,uint64_t,uint32_t,std::vector<uint8_t>&);
inline std::mutex contentReaderMutex;
inline void* contentReaderContext=nullptr;
inline ContentReadFunction contentReadFunction=nullptr;
inline std::atomic<uint64_t> directStorageReads{0},directStorageBytes{0};
inline std::atomic<uint64_t> streamingTextureReads{0},gdeflateDecodedBytes{0};
#if defined(_WIN32)
using StreamingTextureFunction=bool(*)(void*,const std::filesystem::path&,ID3D12Resource**,uint32_t&,uint32_t&,uint32_t&);
inline StreamingTextureFunction streamingTextureFunction=nullptr;
inline void RegisterStreamingTextureReader(StreamingTextureFunction reader){std::lock_guard<std::mutex> lock(contentReaderMutex);streamingTextureFunction=reader;}
inline bool TryReadStreamingTexture(const std::filesystem::path& file,ID3D12Resource** output,uint32_t& width,uint32_t& height,uint32_t& mips){
    std::lock_guard<std::mutex> lock(contentReaderMutex);
    return contentReaderContext&&streamingTextureFunction&&streamingTextureFunction(contentReaderContext,file,output,width,height,mips);
}
#endif
inline void RegisterContentReader(void* context,ContentReadFunction reader) {
    std::lock_guard<std::mutex> lock(contentReaderMutex);
    contentReaderContext=context;contentReadFunction=reader;
}
inline void UnregisterContentReader(void* context) {
    std::lock_guard<std::mutex> lock(contentReaderMutex);
    if(contentReaderContext==context){contentReadFunction=nullptr;contentReaderContext=nullptr;
#if defined(_WIN32)
        streamingTextureFunction=nullptr;
#endif
    }
}
inline bool TryReadContentRange(const std::filesystem::path& file,uint64_t offset,uint32_t size,std::vector<uint8_t>& output) {
    std::lock_guard<std::mutex> lock(contentReaderMutex);
    return contentReadFunction && contentReadFunction(contentReaderContext,file,offset,size,output);
}
}
