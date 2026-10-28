#pragma once

#include <DirectXMath.h>
#include <string>
#include <vector>
#include <memory>
#include "Camera.h"

class Renderer;

enum class CameraMode {
    Freecam = 0,
    FPSPlayer = 1,
    CCTV = 2,
    CinematicOrbit = 3,
    LookAtTarget = 4,
    StaticFixed = 5
};

struct CameraProfile {
    uint32_t id = 0;
    std::string name = "Main Camera";
    CameraMode mode = CameraMode::Freecam;
    DirectX::XMFLOAT3 position = { 0.0f, 1.6f, -5.0f };
    float pitch = 0.0f; // degrees (-89..+89)
    float yaw = 0.0f;   // degrees (-360..+360)
    float roll = 0.0f;  // degrees
    float fov = 75.0f;  // degrees (20..130)
    float nearPlane = 0.1f;
    float farPlane = 2000.0f;

    // Movement & Fly settings (for Freecam)
    float flySpeed = 10.0f;

    // CCTV surveillance oscillation settings
    bool isOscillating = true;
    float baseYaw = 0.0f;
    float sweepAngle = 35.0f; // +/- 35 degrees sweep
    float sweepSpeed = 0.4f;  // oscillation frequency
    float oscillationTimer = 0.0f;

    // Orbit settings
    DirectX::XMFLOAT3 orbitTarget = { 0.0f, 1.0f, 0.0f };
    float orbitRadius = 10.0f;
    float orbitSpeed = 0.25f;
    float orbitAngle = 0.0f;
    float orbitElevation = 3.5f;

    // Look-At Target settings
    DirectX::XMFLOAT3 lookAtTarget = { 0.0f, 1.0f, 0.0f };

    // ImGui Window Open State & In-World Viewport Gizmo
    bool isWindowOpen = false;
    bool showGizmoInWorld = true;
};

class CameraSystem {
public:
    static CameraSystem& Get();

    void Initialize(Renderer* renderer);
    void Update(float dt, Camera* activeCamera, Renderer* renderer = nullptr);

    // Camera Creation & Management
    CameraProfile& CreateCamera(const std::string& name = "", CameraMode mode = CameraMode::Freecam, 
                                const DirectX::XMFLOAT3& pos = { 0.0f, 2.0f, 0.0f }, 
                                float pitch = 0.0f, float yaw = 0.0f, float fov = 75.0f);
    CameraProfile& CreateCameraFromView(Camera* currentCamera, Renderer* renderer = nullptr, const std::string& name = "");
    void DuplicateCamera(size_t index);
    void AddCamera(const CameraProfile& profile);
    void RemoveCamera(size_t index);
    void ClearCameras();
    void ReplaceProfiles(const std::vector<CameraProfile>& profiles,size_t active){
        m_cameras=profiles;m_nextCameraId=1;for(auto& c:m_cameras)c.id=m_nextCameraId++;
        m_activeCameraIndex=m_cameras.empty()?0:(std::min)(active,m_cameras.size()-1);m_selectedIndex=m_activeCameraIndex;
    }

    size_t GetCameraCount() const { return m_cameras.size(); }
    CameraProfile& GetCameraProfile(size_t index) { return m_cameras[index]; }
    const CameraProfile& GetCameraProfile(size_t index) const { return m_cameras[index]; }

    // Selection & Active Camera
    size_t GetSelectedIndex() const { return m_selectedIndex; }
    void SetSelectedIndex(size_t index, bool openWindow = true);
    size_t GetActiveIndex() const { return m_activeCameraIndex; }
    void SetActiveIndex(size_t index, Camera* activeCamera, Renderer* renderer = nullptr);
    bool IsActive(size_t index) const { return index == m_activeCameraIndex; }

    void CycleNextCamera(Camera* activeCamera, Renderer* renderer = nullptr);
    void CyclePrevCamera(Camera* activeCamera, Renderer* renderer = nullptr);

    // Window controls
    void OpenCameraWindow(size_t index);
    void CloseCameraWindow(size_t index);
    void ToggleCameraWindow(size_t index);

    // Synchronization with Camera
    void SyncToCamera(Camera* targetCam, size_t index, Renderer* renderer = nullptr);
    void SyncFromCamera(const Camera* targetCam, size_t index, Renderer* renderer = nullptr);

    // UI Drawing
    void DrawUI(Renderer* renderer, Camera* activeCamera, float screenWidth = 1920.0f, float screenHeight = 1080.0f);
    void DrawSurveillanceUI(Renderer* renderer, Camera* activeCamera) { DrawUI(renderer, activeCamera); }

    bool IsUIOpen() const { return m_showManagerWindow; }
    void SetUIOpen(bool open) { m_showManagerWindow = open; }
    void ToggleUI() { m_showManagerWindow = !m_showManagerWindow; }
    bool IsSurveillanceWindowOpen() const { return m_showManagerWindow; }
    void SetSurveillanceWindowOpen(bool open) { m_showManagerWindow = open; }

private:
    CameraSystem() = default;
    ~CameraSystem() = default;

    void DrawCameraManagerWindow(Renderer* renderer, Camera* activeCamera);
    void DrawIndividualCameraWindow(size_t index, Renderer* renderer, Camera* activeCamera);
    void DrawRadarWidget(CameraProfile& cam);
    void DrawWorldCameraGizmos(Renderer* renderer, Camera* activeCamera, float screenWidth, float screenHeight);

private:
    std::vector<CameraProfile> m_cameras;
    size_t m_activeCameraIndex = 0;
    size_t m_selectedIndex = 0;
    uint32_t m_nextCameraId = 1;

    bool m_showManagerWindow = false;
    bool m_initialized = false;
};
