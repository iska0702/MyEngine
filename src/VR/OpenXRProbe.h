#pragma once

#if defined(_WIN32)
#include <windows.h>
#include <d3d12.h>
#include <string>
#include <vector>
#include <cstring>

#define XR_NO_PROTOTYPES
#define XR_USE_GRAPHICS_API_D3D12
#include "../../ThirdParty/OpenSource/OpenXR/include/openxr/openxr.h"
#include "../../ThirdParty/OpenSource/OpenXR/include/openxr/openxr_platform.h"

namespace EngineVR {
struct ProbeResult {
    std::string status;
    std::string runtime;
    std::string headset;
    bool graphicsCompatible = false;
};

// Read-only runtime probe. Rendering and input require a separate OpenXR session.
inline ProbeResult Probe(ID3D12Device* device) {
    ProbeResult out;
    HMODULE loader = LoadLibraryExW(L"openxr_loader.dll", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!loader) {
        out.status = "OpenXR loader not found. Install a VR runtime and place its official openxr_loader.dll beside the executable.";
        return out;
    }
    struct ModuleGuard { HMODULE module; ~ModuleGuard() { FreeLibrary(module); } } module{loader};
    auto getProc = reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(loader, "xrGetInstanceProcAddr"));
    if (!getProc) { out.status = "OpenXR loader has no xrGetInstanceProcAddr export."; return out; }
    auto get = [&](XrInstance instance, const char* name, PFN_xrVoidFunction* fn) {
        return XR_SUCCEEDED(getProc(instance, name, fn)) && *fn;
    };
    PFN_xrEnumerateInstanceExtensionProperties extensions = nullptr;
    PFN_xrCreateInstance create = nullptr;
    if (!get(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties", reinterpret_cast<PFN_xrVoidFunction*>(&extensions)) ||
        !get(XR_NULL_HANDLE, "xrCreateInstance", reinterpret_cast<PFN_xrVoidFunction*>(&create))) {
        out.status = "OpenXR loader does not provide required entry points."; return out;
    }
    uint32_t count = 0;
    XrResult result = extensions(nullptr, 0, &count, nullptr);
    if (XR_FAILED(result)) { out.status = "Cannot enumerate OpenXR extensions (error " + std::to_string(result) + "). Check the active runtime."; return out; }
    std::vector<XrExtensionProperties> props(count, {XR_TYPE_EXTENSION_PROPERTIES});
    result = extensions(nullptr, count, &count, props.data());
    if (XR_FAILED(result)) { out.status = "Cannot read OpenXR extensions (error " + std::to_string(result) + ")."; return out; }
    bool d3d12 = false;
    for (const auto& p : props) if (std::strcmp(p.extensionName, XR_KHR_D3D12_ENABLE_EXTENSION_NAME) == 0) d3d12 = true;
    if (!d3d12) { out.status = "The active OpenXR runtime does not support DirectX 12."; return out; }
    const char* enabled[] = {XR_KHR_D3D12_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strncpy(ci.applicationInfo.applicationName, "Idontknow", XR_MAX_APPLICATION_NAME_SIZE - 1);
    std::strncpy(ci.applicationInfo.engineName, "Idontknow", XR_MAX_ENGINE_NAME_SIZE - 1);
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ci.enabledExtensionCount = 1; ci.enabledExtensionNames = enabled;
    XrInstance instance = XR_NULL_HANDLE;
    result = create(&ci, &instance);
    if (XR_FAILED(result)) { out.status = "Cannot create OpenXR instance (error " + std::to_string(result) + ")."; return out; }
    PFN_xrDestroyInstance destroy = nullptr;
    if (!get(instance, "xrDestroyInstance", reinterpret_cast<PFN_xrVoidFunction*>(&destroy))) {
        out.status = "OpenXR runtime does not provide xrDestroyInstance."; return out;
    }
    struct InstanceGuard { XrInstance instance; PFN_xrDestroyInstance destroy; ~InstanceGuard() { destroy(instance); } } guard{instance, destroy};
    PFN_xrGetInstanceProperties instanceProps = nullptr;
    PFN_xrGetSystem getSystem = nullptr;
    PFN_xrGetSystemProperties systemProps = nullptr;
    PFN_xrGetD3D12GraphicsRequirementsKHR requirements = nullptr;
    if (!get(instance, "xrGetInstanceProperties", reinterpret_cast<PFN_xrVoidFunction*>(&instanceProps)) ||
        !get(instance, "xrGetSystem", reinterpret_cast<PFN_xrVoidFunction*>(&getSystem)) ||
        !get(instance, "xrGetSystemProperties", reinterpret_cast<PFN_xrVoidFunction*>(&systemProps)) ||
        !get(instance, "xrGetD3D12GraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction*>(&requirements))) {
        out.status = "OpenXR runtime is missing required D3D12 functions."; return out;
    }
    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(instanceProps(instance, &ip))) out.runtime = ip.runtimeName;
    XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO}; si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    result = getSystem(instance, &si, &system);
    if (XR_FAILED(result)) { out.status = "No available OpenXR headset (error " + std::to_string(result) + ")."; return out; }
    XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
    if (XR_SUCCEEDED(systemProps(instance, system, &sp))) out.headset = sp.systemName;
    XrGraphicsRequirementsD3D12KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
    result = requirements(instance, system, &req);
    if (XR_FAILED(result)) { out.status = "Cannot query OpenXR D3D12 requirements (error " + std::to_string(result) + ")."; return out; }
    if (!device) { out.status = "Renderer device is unavailable."; return out; }
    const LUID actual = device->GetAdapterLuid();
    if (actual.HighPart != req.adapterLuid.HighPart || actual.LowPart != req.adapterLuid.LowPart) {
        out.status = "The headset and engine use different graphics adapters."; return out;
    }
    D3D12_FEATURE_DATA_FEATURE_LEVELS levels{};
    const D3D_FEATURE_LEVEL requested[] = {req.minFeatureLevel};
    levels.NumFeatureLevels = 1; levels.pFeatureLevelsRequested = requested;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels))) || levels.MaxSupportedFeatureLevel < req.minFeatureLevel) {
        out.status = "The renderer does not meet the headset's DirectX feature level."; return out;
    }
    out.graphicsCompatible = true;
    out.status = "Runtime and renderer are compatible. VR stereo rendering and controllers are not yet implemented.";
    return out;
}
}
#endif
