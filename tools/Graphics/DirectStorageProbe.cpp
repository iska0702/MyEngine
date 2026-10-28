#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dstorage.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
using Microsoft::WRL::ComPtr;
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    auto directory = std::filesystem::absolute(argv[1]);
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
    AddDllDirectory(directory.c_str());
    auto core = LoadLibraryExW((directory / L"dstoragecore.dll").c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    std::cout << "core_load " << (core != nullptr) << " error " << (core ? 0 : GetLastError()) << '\n';
    auto dll = LoadLibraryExW((directory / L"dstorage.dll").c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!dll) { std::cout << "load_error " << GetLastError() << '\n'; return 3; }
    auto configure = reinterpret_cast<decltype(&DStorageSetConfiguration)>(GetProcAddress(dll, "DStorageSetConfiguration"));
    auto getFactory = reinterpret_cast<decltype(&DStorageGetFactory)>(GetProcAddress(dll, "DStorageGetFactory"));
    if (!configure || !getFactory) return 4;
    DSTORAGE_CONFIGURATION config{};
    auto hr = configure(&config);
    std::cout << "configuration 0x" << std::hex << hr << '\n';
    ComPtr<IDStorageFactory> factory;
    hr = getFactory(IID_PPV_ARGS(&factory));
    std::cout << "factory 0x" << std::hex << hr << '\n';
    for (auto name : {L"dstorage.dll", L"dstoragecore.dll"}) {
        wchar_t path[32768]{};
        if (auto module = GetModuleHandleW(name)) {
            GetModuleFileNameW(module, path, 32768); std::wcout << name << L" " << path << L'\n';
        }
    }
    if (FAILED(hr)) return 5;
    ComPtr<ID3D12Debug> debug;
    const auto debugResult=D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
    std::cout << "d3d12_debug_interface 0x" << std::hex << debugResult << '\n';
    ComPtr<ID3D12Device> device;
    hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
    if (FAILED(hr)) return 6;
    DSTORAGE_QUEUE_DESC desc{};
    desc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE; desc.Capacity=128;
    desc.Priority=DSTORAGE_PRIORITY_NORMAL; desc.Device=device.Get();
    ComPtr<IDStorageQueue> queue;
    hr = factory->CreateQueue(&desc, IID_PPV_ARGS(&queue));
    std::cout << "queue 0x" << std::hex << hr << '\n';
    if (FAILED(hr)) return 7;
    auto fixture = std::filesystem::temp_directory_path() /
        (L"Idontknow-DirectStorage-probe-" + std::to_wstring(GetCurrentProcessId()) + L".bin");
    std::vector<unsigned char> expected(16384), actual(16384);
    for (size_t i=0; i<expected.size(); ++i) expected[i]=static_cast<unsigned char>(i*37+11);
    { std::ofstream file(fixture,std::ios::binary); file.write(reinterpret_cast<const char*>(expected.data()),expected.size()); }
    ComPtr<IDStorageFile> file;
    hr = factory->OpenFile(fixture.c_str(), IID_PPV_ARGS(&file));
    if (FAILED(hr)) return 8;
    DSTORAGE_REQUEST request{};
    request.Options.SourceType=DSTORAGE_REQUEST_SOURCE_FILE;
    request.Options.DestinationType=DSTORAGE_REQUEST_DESTINATION_MEMORY;
    request.Source.File.Source=file.Get(); request.Source.File.Size=static_cast<UINT32>(expected.size());
    request.Destination.Memory.Buffer=actual.data(); request.Destination.Memory.Size=static_cast<UINT32>(actual.size());
    request.UncompressedSize=static_cast<UINT32>(actual.size());
    ComPtr<ID3D12Fence> fence;
    if (FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)))) return 9;
    auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if (!event) return 10;
    fence->SetEventOnCompletion(1,event);
    queue->EnqueueRequest(&request); queue->EnqueueSignal(fence.Get(),1); queue->Submit();
    auto wait=WaitForSingleObject(event,10000);
    CloseHandle(event);
    DSTORAGE_ERROR_RECORD error{}; queue->RetrieveErrorRecord(&error);
    const bool passed=wait==WAIT_OBJECT_0 && SUCCEEDED(error.FirstFailure.HResult) && actual==expected;
    std::cout << "read_equal " << passed << " error 0x" << std::hex << error.FirstFailure.HResult << '\n';
    file.Reset(); std::error_code ec; std::filesystem::remove(fixture,ec);
    return passed ? 0 : 11;
}
