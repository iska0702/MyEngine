#pragma once
#include "OpenXRProbe.h"
#if defined(_WIN32)
namespace EngineVR {
// Experimental head-tracking session. No layers are submitted until stereo rendering exists.
class Session {
public:
    static Session& Get() { static Session s; return s; }
    ~Session() { Stop(); }
    bool Start(ID3D12Device* device, ID3D12CommandQueue* queue) {
        Stop();
        if (!device || !queue) return Fail("D3D12 device or command queue unavailable.");
        const auto probe = Probe(device);
        if (!probe.graphicsCompatible) return Fail(probe.status);
        m_loader = LoadLibraryExW(L"openxr_loader.dll", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!m_loader) return Fail("OpenXR loader unavailable.");
        m_get = reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(m_loader, "xrGetInstanceProcAddr"));
        PFN_xrCreateInstance create = nullptr;
        if (!m_get || !Load(XR_NULL_HANDLE, "xrCreateInstance", create)) return Fail("OpenXR loader is missing xrCreateInstance.");
        const char* extensions[] = {XR_KHR_D3D12_ENABLE_EXTENSION_NAME};
        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
        std::strncpy(info.applicationInfo.applicationName, "Idontknow", XR_MAX_APPLICATION_NAME_SIZE - 1);
        std::strncpy(info.applicationInfo.engineName, "Idontknow", XR_MAX_ENGINE_NAME_SIZE - 1);
        info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
        info.enabledExtensionCount = 1; info.enabledExtensionNames = extensions;
        XrResult result = create(&info, &m_instance);
        if (XR_FAILED(result)) return FailResult("xrCreateInstance", result);
#define IDK_XR_LOAD(member, name) if (!Load(m_instance, #name, member)) return Fail("Missing OpenXR function: " #name)
        IDK_XR_LOAD(m_destroyInstance, xrDestroyInstance);
        IDK_XR_LOAD(m_getSystem, xrGetSystem);
        IDK_XR_LOAD(m_requirements, xrGetD3D12GraphicsRequirementsKHR);
        IDK_XR_LOAD(m_createSession, xrCreateSession);
        IDK_XR_LOAD(m_destroySession, xrDestroySession);
        IDK_XR_LOAD(m_createSpace, xrCreateReferenceSpace);
        IDK_XR_LOAD(m_destroySpace, xrDestroySpace);
        IDK_XR_LOAD(m_pollEvent, xrPollEvent);
        IDK_XR_LOAD(m_beginSession, xrBeginSession);
        IDK_XR_LOAD(m_endSession, xrEndSession);
        IDK_XR_LOAD(m_waitFrame, xrWaitFrame);
        IDK_XR_LOAD(m_beginFrame, xrBeginFrame);
        IDK_XR_LOAD(m_endFrame, xrEndFrame);
        IDK_XR_LOAD(m_locateViews, xrLocateViews);
#undef IDK_XR_LOAD
        XrSystemGetInfo sys{XR_TYPE_SYSTEM_GET_INFO}; sys.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        result = m_getSystem(m_instance, &sys, &m_system);
        if (XR_FAILED(result)) return FailResult("xrGetSystem", result);
        XrGraphicsRequirementsD3D12KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
        result = m_requirements(m_instance, m_system, &req);
        if (XR_FAILED(result)) return FailResult("xrGetD3D12GraphicsRequirementsKHR", result);
        const LUID actual = device->GetAdapterLuid();
        if (actual.HighPart != req.adapterLuid.HighPart || actual.LowPart != req.adapterLuid.LowPart)
            return Fail("Headset and renderer use different D3D12 adapters.");
        XrGraphicsBindingD3D12KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR}; binding.device = device; binding.queue = queue;
        XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO}; sci.next = &binding; sci.systemId = m_system;
        result = m_createSession(m_instance, &sci, &m_session);
        if (XR_FAILED(result)) return FailResult("xrCreateSession", result);
        XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL; space.poseInReferenceSpace.orientation.w = 1.0f;
        result = m_createSpace(m_session, &space, &m_space);
        if (XR_FAILED(result)) return FailResult("xrCreateReferenceSpace", result);
        m_status = "Session created; waiting for the runtime. No image is submitted in tracking test mode.";
        return true;
    }
    void Tick() {
        if (!m_instance || !m_session) return;
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        XrResult poll = XR_SUCCESS;
        while ((poll = m_pollEvent(m_instance, &event)) == XR_SUCCESS) {
            if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) { Fail("OpenXR instance loss pending."); return; }
            if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                const auto& changed = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
                if (changed.session == m_session) {
                    if (changed.state == XR_SESSION_STATE_READY && !m_running) {
                        XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO}; begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        const XrResult r = m_beginSession(m_session, &begin);
                        if (XR_FAILED(r)) { FailResult("xrBeginSession", r); return; }
                        m_running = true; m_status = "Tracking test running; no VR image is rendered.";
                    } else if (changed.state == XR_SESSION_STATE_STOPPING && m_running) {
                        const XrResult r = m_endSession(m_session); m_running = false;
                        if (XR_FAILED(r)) { FailResult("xrEndSession", r); return; }
                    } else if (changed.state == XR_SESSION_STATE_EXITING || changed.state == XR_SESSION_STATE_LOSS_PENDING) {
                        Fail("OpenXR session ended or was lost."); return;
                    }
                }
            }
            event = {XR_TYPE_EVENT_DATA_BUFFER};
        }
        if (poll != XR_EVENT_UNAVAILABLE) { FailResult("xrPollEvent", poll); return; }
        if (!m_running) return;
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO}; XrFrameState state{XR_TYPE_FRAME_STATE};
        XrResult r = m_waitFrame(m_session, &wait, &state);
        if (XR_FAILED(r)) { FailResult("xrWaitFrame", r); return; }
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO}; r = m_beginFrame(m_session, &begin);
        if (XR_FAILED(r)) { FailResult("xrBeginFrame", r); return; }
        m_trackingValid = false;
        if (state.shouldRender) {
            XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO}; locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            locate.displayTime = state.predictedDisplayTime; locate.space = m_space;
            XrViewState viewState{XR_TYPE_VIEW_STATE}; uint32_t count = 0;
            XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
            r = m_locateViews(m_session, &locate, &viewState, 2, &count, views);
            if (XR_SUCCEEDED(r) && count == 2 && (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) && (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
                m_views[0] = views[0]; m_views[1] = views[1]; m_trackingValid = true;
            }
        }
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO}; end.displayTime = state.predictedDisplayTime;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE; end.layerCount = 0;
        r = m_endFrame(m_session, &end);
        if (XR_FAILED(r)) FailResult("xrEndFrame", r);
    }
    void Stop() {
        if (m_space && m_destroySpace) m_destroySpace(m_space);
        if (m_session && m_destroySession) m_destroySession(m_session);
        if (m_instance && m_destroyInstance) m_destroyInstance(m_instance);
        if (m_loader) FreeLibrary(m_loader);
        m_space = XR_NULL_HANDLE; m_session = XR_NULL_HANDLE; m_instance = XR_NULL_HANDLE; m_loader = nullptr;
        m_running = false; m_trackingValid = false;
    }
    bool IsStarted() const { return m_session != XR_NULL_HANDLE; }
    bool TrackingValid() const { return m_trackingValid; }
    const XrView& Eye(unsigned i) const { return m_views[i < 2 ? i : 0]; }
    const std::string& Status() const { return m_status; }
private:
    template<typename T> bool Load(XrInstance instance, const char* name, T& out) {
        return m_get && XR_SUCCEEDED(m_get(instance, name, reinterpret_cast<PFN_xrVoidFunction*>(&out))) && out;
    }
    bool Fail(const std::string& message) { Stop(); m_status = message; return false; }
    bool FailResult(const char* op, XrResult r) { return Fail(std::string(op) + " failed (" + std::to_string(r) + ")."); }
    HMODULE m_loader = nullptr; PFN_xrGetInstanceProcAddr m_get = nullptr;
    XrInstance m_instance = XR_NULL_HANDLE; XrSystemId m_system = XR_NULL_SYSTEM_ID;
    XrSession m_session = XR_NULL_HANDLE; XrSpace m_space = XR_NULL_HANDLE;
    bool m_running = false, m_trackingValid = false; XrView m_views[2]{}; std::string m_status;
    PFN_xrDestroyInstance m_destroyInstance = nullptr; PFN_xrGetSystem m_getSystem = nullptr;
    PFN_xrGetD3D12GraphicsRequirementsKHR m_requirements = nullptr;
    PFN_xrCreateSession m_createSession = nullptr; PFN_xrDestroySession m_destroySession = nullptr;
    PFN_xrCreateReferenceSpace m_createSpace = nullptr; PFN_xrDestroySpace m_destroySpace = nullptr;
    PFN_xrPollEvent m_pollEvent = nullptr; PFN_xrBeginSession m_beginSession = nullptr; PFN_xrEndSession m_endSession = nullptr;
    PFN_xrWaitFrame m_waitFrame = nullptr; PFN_xrBeginFrame m_beginFrame = nullptr; PFN_xrEndFrame m_endFrame = nullptr;
    PFN_xrLocateViews m_locateViews = nullptr;
};
}
#endif
