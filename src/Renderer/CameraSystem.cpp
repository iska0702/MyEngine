#include "CameraSystem.h"
#include "Renderer.h"
#include <cmath>
#include <algorithm>
#include <string>

#ifndef ENGINE_RELEASE
#include "imgui.h"
#endif

using namespace DirectX;

CameraSystem& CameraSystem::Get() {
    static CameraSystem instance;
    return instance;
}

void CameraSystem::Initialize(Renderer* renderer) {
    if (m_initialized) return;

    m_cameras.clear();
    m_nextCameraId = 1;

    // Start with a single clean default camera (zero dummy clutter)
    CameraProfile defaultCam;
    defaultCam.id = m_nextCameraId++;
    defaultCam.name = "Main Camera";
    defaultCam.mode = CameraMode::Freecam;
    defaultCam.position = { 0.0f, 1.6f, -5.0f };
    defaultCam.pitch = 0.0f;
    defaultCam.yaw = 0.0f;
    defaultCam.roll = 0.0f;
    defaultCam.fov = renderer ? renderer->GetFOV() : 75.0f;
    defaultCam.isWindowOpen = false;
    defaultCam.showGizmoInWorld = true;
    m_cameras.push_back(defaultCam);

    m_activeCameraIndex = 0;
    m_selectedIndex = 0;
    m_showManagerWindow = false;
    m_initialized = true;
}

CameraProfile& CameraSystem::CreateCamera(const std::string& name, CameraMode mode, 
                                         const XMFLOAT3& pos, float pitch, float yaw, float fov) {
    CameraProfile cam;
    cam.id = m_nextCameraId++;
    if (name.empty()) {
        char buf[64];
        sprintf_s(buf, "Camera %02zu", m_cameras.size() + 1);
        cam.name = buf;
    } else {
        cam.name = name;
    }
    cam.mode = mode;
    cam.position = pos;
    cam.pitch = pitch;
    cam.yaw = yaw;
    cam.roll = 0.0f;
    cam.fov = fov;
    cam.isWindowOpen = true; // Open dedicated ImGui window for this new camera
    cam.showGizmoInWorld = true;

    // Configure sensible mode defaults
    if (mode == CameraMode::CCTV) {
        cam.baseYaw = yaw;
        cam.sweepAngle = 35.0f;
        cam.sweepSpeed = 0.35f;
        cam.isOscillating = true;
    } else if (mode == CameraMode::CinematicOrbit) {
        cam.orbitTarget = { 0.0f, 1.0f, 0.0f };
        cam.orbitRadius = 10.0f;
        cam.orbitSpeed = 0.25f;
        cam.orbitElevation = 3.5f;
    }

    m_cameras.push_back(cam);
    m_selectedIndex = m_cameras.size() - 1;
    return m_cameras.back();
}

CameraProfile& CameraSystem::CreateCameraFromView(Camera* currentCamera, Renderer* renderer, const std::string& name) {
    XMFLOAT3 pos = currentCamera ? currentCamera->GetPosition() : XMFLOAT3(0.0f, 1.6f, 0.0f);
    float pitch = currentCamera ? currentCamera->GetPitch() : 0.0f;
    float yaw = currentCamera ? currentCamera->GetYaw() : 0.0f;
    float fov = renderer ? renderer->GetFOV() : 75.0f;
    CameraMode mode = (currentCamera && currentCamera->IsFreecamMode()) ? CameraMode::Freecam : CameraMode::FPSPlayer;

    std::string camName = name;
    if (camName.empty()) {
        char buf[64];
        sprintf_s(buf, "View Cam %02zu", m_cameras.size() + 1);
        camName = buf;
    }

    return CreateCamera(camName, mode, pos, pitch, yaw, fov);
}

void CameraSystem::DuplicateCamera(size_t index) {
    if (index >= m_cameras.size()) return;

    CameraProfile copy = m_cameras[index];
    copy.id = m_nextCameraId++;
    copy.name += " (Copy)";
    copy.position.x += 1.0f; // Offset slightly so it's distinguishable in 3D
    copy.isWindowOpen = true;

    m_cameras.push_back(copy);
    m_selectedIndex = m_cameras.size() - 1;
}

void CameraSystem::AddCamera(const CameraProfile& profile) {
    CameraProfile p = profile;
    if (p.id == 0) p.id = m_nextCameraId++;
    else m_nextCameraId=(std::max)(m_nextCameraId,p.id+1);
    m_cameras.push_back(p);
}

void CameraSystem::RemoveCamera(size_t index) {
    if (index >= m_cameras.size() || m_cameras.size() <= 1) return; // Keep at least 1 camera

    // If deleting active camera, fallback to another camera first
    if (m_activeCameraIndex == index) {
        m_activeCameraIndex = (index > 0) ? (index - 1) : 0;
    } else if (m_activeCameraIndex > index) {
        m_activeCameraIndex--;
    }

    m_cameras.erase(m_cameras.begin() + index);

    if (m_selectedIndex >= m_cameras.size()) {
        m_selectedIndex = m_cameras.size() - 1;
    }
}

void CameraSystem::ClearCameras() {
    m_cameras.clear();
    m_activeCameraIndex = 0;
    m_selectedIndex = 0;
    m_initialized=false;Initialize(nullptr);
}

void CameraSystem::SetSelectedIndex(size_t index, bool openWindow) {
    if (index >= m_cameras.size()) return;
    m_selectedIndex = index;
    if (openWindow) {
        m_cameras[index].isWindowOpen = true;
    }
}

void CameraSystem::SetActiveIndex(size_t index, Camera* activeCamera, Renderer* renderer) {
    if (index >= m_cameras.size() || !activeCamera) return;

    // Save previous active camera transform before switching
    if (m_activeCameraIndex < m_cameras.size()) {
        auto& oldCam = m_cameras[m_activeCameraIndex];
        if (oldCam.mode == CameraMode::FPSPlayer || oldCam.mode == CameraMode::Freecam) {
            SyncFromCamera(activeCamera, m_activeCameraIndex, renderer);
        }
    }

    m_activeCameraIndex = index;
    m_selectedIndex = index;
    auto& newCam = m_cameras[m_activeCameraIndex];

    // Configure underlying Camera mode
    if (newCam.mode == CameraMode::FPSPlayer) {
        activeCamera->SetFreecamMode(false);
        activeCamera->SetCollisionEnabled(true);
    } else {
        // Freecam, CCTV, Orbit, LookAt, Static are all free observation views
        activeCamera->SetFreecamMode(true);
        activeCamera->SetCollisionEnabled(false);
    }

    SyncToCamera(activeCamera, m_activeCameraIndex, renderer);
}

void CameraSystem::CycleNextCamera(Camera* activeCamera, Renderer* renderer) {
    if (m_cameras.empty() || !activeCamera) return;
    size_t nextIdx = (m_activeCameraIndex + 1) % m_cameras.size();
    SetActiveIndex(nextIdx, activeCamera, renderer);
}

void CameraSystem::CyclePrevCamera(Camera* activeCamera, Renderer* renderer) {
    if (m_cameras.empty() || !activeCamera) return;
    size_t prevIdx = (m_activeCameraIndex + m_cameras.size() - 1) % m_cameras.size();
    SetActiveIndex(prevIdx, activeCamera, renderer);
}

void CameraSystem::OpenCameraWindow(size_t index) {
    if (index < m_cameras.size()) {
        m_cameras[index].isWindowOpen = true;
        m_selectedIndex = index;
    }
}

void CameraSystem::CloseCameraWindow(size_t index) {
    if (index < m_cameras.size()) {
        m_cameras[index].isWindowOpen = false;
    }
}

void CameraSystem::ToggleCameraWindow(size_t index) {
    if (index < m_cameras.size()) {
        m_cameras[index].isWindowOpen = !m_cameras[index].isWindowOpen;
        if (m_cameras[index].isWindowOpen) {
            m_selectedIndex = index;
        }
    }
}

void CameraSystem::SyncToCamera(Camera* targetCam, size_t index, Renderer* renderer) {
    if (!targetCam || index >= m_cameras.size()) return;
    const auto& p = m_cameras[index];
    targetCam->SetPosition(p.position);
    targetCam->SetYaw(p.yaw);
    targetCam->SetPitch(p.pitch);
    if (renderer) {
        renderer->SetFOV(p.fov);
    }
}

void CameraSystem::SyncFromCamera(const Camera* targetCam, size_t index, Renderer* renderer) {
    if (!targetCam || index >= m_cameras.size()) return;
    auto& p = m_cameras[index];
    p.position = targetCam->GetPosition();
    p.yaw = targetCam->GetYaw();
    p.pitch = targetCam->GetPitch();
    if (renderer) {
        p.fov = renderer->GetFOV();
    }
}

void CameraSystem::Update(float dt, Camera* activeCamera, Renderer* renderer) {
    if (m_cameras.empty() || !activeCamera) return;

    // 1. Procedural behaviors for non-interactive camera modes
    for (size_t i = 0; i < m_cameras.size(); ++i) {
        auto& cam = m_cameras[i];

        if (cam.mode == CameraMode::CCTV) {
            if (cam.isOscillating) {
                cam.oscillationTimer += dt * cam.sweepSpeed * 6.2831853f;
                cam.yaw = cam.baseYaw + sinf(cam.oscillationTimer) * cam.sweepAngle;
            }
        } else if (cam.mode == CameraMode::CinematicOrbit) {
            cam.orbitAngle += dt * cam.orbitSpeed;
            cam.position.x = cam.orbitTarget.x + cosf(cam.orbitAngle) * cam.orbitRadius;
            cam.position.z = cam.orbitTarget.z + sinf(cam.orbitAngle) * cam.orbitRadius;
            cam.position.y = cam.orbitTarget.y + cam.orbitElevation;

            float dx = cam.orbitTarget.x - cam.position.x;
            float dy = cam.orbitTarget.y - cam.position.y;
            float dz = cam.orbitTarget.z - cam.position.z;
            float horizDist = sqrtf(dx * dx + dz * dz);

            cam.yaw = XMConvertToDegrees(atan2f(dx, dz));
            cam.pitch = XMConvertToDegrees(atan2f(dy, horizDist));
        } else if (cam.mode == CameraMode::LookAtTarget) {
            float dx = cam.lookAtTarget.x - cam.position.x;
            float dy = cam.lookAtTarget.y - cam.position.y;
            float dz = cam.lookAtTarget.z - cam.position.z;
            float horizDist = sqrtf(dx * dx + dz * dz);

            cam.yaw = XMConvertToDegrees(atan2f(dx, dz));
            cam.pitch = XMConvertToDegrees(atan2f(dy, horizDist));
        }
    }

    // 2. Synchronization with the active underlying engine Camera
    auto& activeProfile = m_cameras[m_activeCameraIndex];
    if (activeProfile.mode == CameraMode::FPSPlayer || activeProfile.mode == CameraMode::Freecam) {
        SyncFromCamera(activeCamera, m_activeCameraIndex, renderer);
    } else {
        SyncToCamera(activeCamera, m_activeCameraIndex, renderer);
    }
}

#ifndef ENGINE_RELEASE

void CameraSystem::DrawRadarWidget(CameraProfile& cam) {
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Orientation & FOV Radar (Top-Down)");
    
    ImVec2 canvasSize(180.0f, 180.0f);
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Radar background disc
    ImVec2 center(canvasPos.x + canvasSize.x * 0.5f, canvasPos.y + canvasSize.y * 0.5f);
    float radius = canvasSize.x * 0.45f;

    drawList->AddCircleFilled(center, radius, IM_COL32(16, 22, 32, 240), 36);
    drawList->AddCircle(center, radius * 0.33f, IM_COL32(40, 70, 100, 120), 24, 1.0f);
    drawList->AddCircle(center, radius * 0.66f, IM_COL32(40, 70, 100, 120), 24, 1.0f);
    drawList->AddCircle(center, radius, IM_COL32(70, 130, 190, 255), 36, 1.5f);

    // Crosshairs
    drawList->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y), IM_COL32(50, 90, 130, 160), 1.0f);
    drawList->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius), IM_COL32(50, 90, 130, 160), 1.0f);

    // Cardinal directions
    drawList->AddText(ImVec2(center.x - 4, center.y - radius + 4), IM_COL32(180, 210, 240, 200), "N");
    drawList->AddText(ImVec2(center.x - 4, center.y + radius - 16), IM_COL32(180, 210, 240, 200), "S");
    drawList->AddText(ImVec2(center.x + radius - 14, center.y - 6), IM_COL32(180, 210, 240, 200), "E");
    drawList->AddText(ImVec2(center.x - radius + 4, center.y - 6), IM_COL32(180, 210, 240, 200), "W");

    // View direction & FOV cone (wedge)
    float yawRad = XMConvertToRadians(cam.yaw);
    float halfFovRad = XMConvertToRadians(cam.fov * 0.5f);
    float leftAngle = yawRad - halfFovRad;
    float rightAngle = yawRad + halfFovRad;

    // Note: in 2D canvas, North is -Y, East is +X
    ImVec2 pCenter = center;
    ImVec2 pLeft(center.x + sinf(leftAngle) * radius * 0.9f, center.y - cosf(leftAngle) * radius * 0.9f);
    ImVec2 pRight(center.x + sinf(rightAngle) * radius * 0.9f, center.y - cosf(rightAngle) * radius * 0.9f);
    ImVec2 pForward(center.x + sinf(yawRad) * radius * 0.95f, center.y - cosf(yawRad) * radius * 0.95f);

    // FOV Wedge fill
    drawList->AddTriangleFilled(pCenter, pLeft, pRight, IM_COL32(30, 180, 255, 60));
    drawList->AddLine(pCenter, pLeft, IM_COL32(40, 200, 255, 180), 1.5f);
    drawList->AddLine(pCenter, pRight, IM_COL32(40, 200, 255, 180), 1.5f);
    drawList->AddLine(pCenter, pForward, IM_COL32(255, 220, 40, 240), 2.0f);

    // Camera origin point
    drawList->AddCircleFilled(center, 4.0f, IM_COL32(255, 80, 80, 255));
    drawList->AddCircle(center, 6.0f, IM_COL32(255, 255, 255, 220), 12, 1.5f);

    // Pitch indicator bar on right edge of canvas
    float pitchBarH = radius * 1.6f;
    float pitchBarX = canvasPos.x + canvasSize.x + 10.0f;
    float pitchBarY = center.y - pitchBarH * 0.5f;
    drawList->AddRectFilled(ImVec2(pitchBarX, pitchBarY), ImVec2(pitchBarX + 12.0f, pitchBarY + pitchBarH), IM_COL32(20, 30, 45, 220), 3.0f);
    
    // Normalized pitch indicator (-89..+89 -> 0..1)
    float normPitch = (cam.pitch + 89.0f) / 178.0f;
    normPitch = (std::clamp)(normPitch, 0.0f, 1.0f);
    float handleY = pitchBarY + (1.0f - normPitch) * pitchBarH;
    drawList->AddCircleFilled(ImVec2(pitchBarX + 6.0f, handleY), 5.0f, IM_COL32(255, 200, 50, 255));

    // Interactive dragging to rotate yaw
    ImGui::InvisibleButton("RadarHitbox", ImVec2(canvasSize.x + 40.0f, canvasSize.y));
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImVec2 mouse = ImGui::GetIO().MousePos;
        float dx = mouse.x - center.x;
        float dy = -(mouse.y - center.y);
        float newYaw = XMConvertToDegrees(atan2f(dx, dy));
        if (newYaw < 0.0f) newYaw += 360.0f;
        cam.yaw = newYaw;
    }

    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Text("Pitch: %.1f°", cam.pitch);
    ImGui::Text("Yaw:   %.1f°", cam.yaw);
    ImGui::Text("FOV:   %.1f°", cam.fov);
    ImGui::TextDisabled("(Drag radar to rotate)");
    ImGui::EndGroup();
}

void CameraSystem::DrawIndividualCameraWindow(size_t index, Renderer* renderer, Camera* activeCamera) {
    if (index >= m_cameras.size()) return;
    auto& cam = m_cameras[index];
    if (!cam.isWindowOpen) return;

    bool isActive = (index == m_activeCameraIndex);

    std::string winTitle = "Camera: " + cam.name + (isActive ? " [ACTIVE]###CamWin_" : "###CamWin_") + std::to_string(cam.id);
    ImGui::SetNextWindowSize(ImVec2(480, 640), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(winTitle.c_str(), &cam.isWindowOpen)) {
        ImGui::End();
        return;
    }

    // 1. Header & Active View Switcher
    if (isActive) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.55f, 0.22f, 1.0f));
        ImGui::Button("[ACTIVE VIEWPORT CAMERA]", ImVec2(-1, 32));
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.85f, 1.0f));
        if (ImGui::Button("LOOK THROUGH THIS CAMERA (Switch View)", ImVec2(-1, 32))) {
            SetActiveIndex(index, activeCamera, renderer);
        }
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();

    // 2. Alignment & Teleport Buttons
    if (ImGui::Button("Snap to Current View")) {
        if (activeCamera) {
            cam.position = activeCamera->GetPosition();
            cam.pitch = activeCamera->GetPitch();
            cam.yaw = activeCamera->GetYaw();
            if (renderer) cam.fov = renderer->GetFOV();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Teleport View to Camera")) {
        if (activeCamera) {
            activeCamera->SetPosition(cam.position);
            activeCamera->SetPitch(cam.pitch);
            activeCamera->SetYaw(cam.yaw);
            if (renderer) renderer->SetFOV(cam.fov);
        }
    }
    ImGui::SameLine();
    ImGui::Checkbox("3D Gizmo", &cam.showGizmoInWorld);

    ImGui::Separator();

    // 3. Interactive Radar & Orientation Widget
    DrawRadarWidget(cam);

    ImGui::Separator();

    // 4. Transform Controls
    if (ImGui::CollapsingHeader("Transform & Coordinates", ImGuiTreeNodeFlags_DefaultOpen)) {
        char nameBuf[64];
        strncpy_s(nameBuf, cam.name.c_str(), sizeof(nameBuf) - 1);
        if (ImGui::InputText("Camera Name", nameBuf, sizeof(nameBuf))) {
            cam.name = nameBuf;
        }

        const char* modeNames[] = { 
            "Manual Freecam", 
            "First-Person Player", 
            "CCTV Surveillance", 
            "Cinematic Orbit", 
            "Look-At Target", 
            "Static Fixed"
        };
        int curMode = (int)cam.mode;
        if (ImGui::Combo("Camera Mode", &curMode, modeNames, IM_ARRAYSIZE(modeNames))) {
            cam.mode = (CameraMode)curMode;
            if (isActive) SetActiveIndex(index, activeCamera, renderer);
        }

        if (ImGui::DragFloat3("Position (X, Y, Z)", &cam.position.x, 0.1f, -1000.0f, 1000.0f, "%.2f m")) {
            if (isActive && activeCamera) activeCamera->SetPosition(cam.position);
        }

        if (ImGui::DragFloat("Pitch", &cam.pitch, 0.5f, -89.0f, 89.0f, "%.1f deg")) {
            if (isActive && activeCamera) activeCamera->SetPitch(cam.pitch);
        }

        if (ImGui::DragFloat("Yaw", &cam.yaw, 0.5f, -360.0f, 360.0f, "%.1f deg")) {
            if (isActive && activeCamera) activeCamera->SetYaw(cam.yaw);
        }

        ImGui::DragFloat("Roll", &cam.roll, 0.5f, -180.0f, 180.0f, "%.1f deg");
    }

    // 5. Optics & Lens Parameters
    if (ImGui::CollapsingHeader("Optics & Lens Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SliderFloat("Field of View (FOV)", &cam.fov, 20.0f, 130.0f, "%.1f deg")) {
            if (isActive && renderer) renderer->SetFOV(cam.fov);
        }
        ImGui::DragFloat("Near Clip Plane", &cam.nearPlane, 0.01f, 0.01f, 10.0f, "%.2f m");
        ImGui::DragFloat("Far Clip Plane", &cam.farPlane, 10.0f, 10.0f, 10000.0f, "%.0f m");
    }

    // 6. Mode-Specific Behavior Parameters
    if (cam.mode == CameraMode::CCTV) {
        if (ImGui::CollapsingHeader("CCTV Surveillance Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Auto-Oscillation", &cam.isOscillating);
            if (cam.isOscillating) {
                ImGui::SliderFloat("Base Yaw", &cam.baseYaw, -180.0f, 180.0f, "%.1f deg");
                ImGui::SliderFloat("Sweep Angle", &cam.sweepAngle, 5.0f, 90.0f, "+/- %.1f deg");
                ImGui::SliderFloat("Sweep Speed", &cam.sweepSpeed, 0.05f, 2.0f, "%.2f Hz");
            }
        }
    } else if (cam.mode == CameraMode::CinematicOrbit) {
        if (ImGui::CollapsingHeader("Cinematic Orbit Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat3("Orbit Target", &cam.orbitTarget.x, 0.1f);
            ImGui::SliderFloat("Orbit Radius", &cam.orbitRadius, 1.0f, 60.0f, "%.1f m");
            ImGui::SliderFloat("Orbit Speed", &cam.orbitSpeed, -2.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Elevation Height", &cam.orbitElevation, -5.0f, 25.0f, "%.1f m");
        }
    } else if (cam.mode == CameraMode::LookAtTarget) {
        if (ImGui::CollapsingHeader("Look-At Target Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat3("Target Position", &cam.lookAtTarget.x, 0.1f);
            if (activeCamera && ImGui::Button("Set Target to Current Player View")) {
                cam.lookAtTarget = activeCamera->GetPosition();
            }
        }
    } else if (cam.mode == CameraMode::Freecam) {
        if (ImGui::CollapsingHeader("Free Flight Controls")) {
            ImGui::SliderFloat("Fly Speed", &cam.flySpeed, 1.0f, 50.0f, "%.1f m/s");
        }
    }

    ImGui::Separator();

    // 7. Management Actions
    if (ImGui::Button("Duplicate Camera")) {
        DuplicateCamera(index);
    }
    ImGui::SameLine();
    if (m_cameras.size() > 1) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Delete Camera")) {
            RemoveCamera(index);
            ImGui::PopStyleColor();
            ImGui::End();
            return;
        }
        ImGui::PopStyleColor();
    }

    ImGui::End();
}

void CameraSystem::DrawCameraManagerWindow(Renderer* renderer, Camera* activeCamera) {
    if (!m_showManagerWindow) return;

    ImGui::SetNextWindowSize(ImVec2(540, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Camera Studio & Manager", &m_showManagerWindow, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "CAMERA STUDIO - HIERARCHY & CONTROLS");
    ImGui::Separator();

    // Creator toolbar
    ImGui::Text("Create New Camera:");
    if (ImGui::Button("+ From Current View")) {
        CreateCameraFromView(activeCamera, renderer);
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Freecam")) {
        XMFLOAT3 pos = activeCamera ? activeCamera->GetPosition() : XMFLOAT3(0, 2, 0);
        CreateCamera("Freecam", CameraMode::Freecam, pos, 0, 0, 75.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("+ CCTV")) {
        XMFLOAT3 pos = activeCamera ? activeCamera->GetPosition() : XMFLOAT3(10, 6, 10);
        CreateCamera("CCTV Security", CameraMode::CCTV, pos, -20.0f, 180.0f, 65.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Orbit")) {
        XMFLOAT3 pos = { 0.0f, 3.5f, 10.0f };
        CreateCamera("Orbit Cam", CameraMode::CinematicOrbit, pos, -15.0f, 0.0f, 70.0f);
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Quick Switch: Hotkey 'C' cycles through active cameras");
    ImGui::Separator();

    // Cameras List
    ImGui::Text("Cameras in Scene (%zu):", m_cameras.size());
    ImGui::BeginChild("CamerasList", ImVec2(0, 0), true);

    for (size_t i = 0; i < m_cameras.size(); ++i) {
        auto& cam = m_cameras[i];
        bool isActive = (i == m_activeCameraIndex);
        bool isSelected = (i == m_selectedIndex);

        ImGui::PushID((int)i);

        // Header color
        ImVec4 col = isActive ? ImVec4(0.2f, 0.85f, 0.4f, 1.0f) :
                     isSelected ? ImVec4(0.3f, 0.7f, 1.0f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, col);

        const char* modeTag = (cam.mode == CameraMode::FPSPlayer) ? "[PLAYER]" :
                              (cam.mode == CameraMode::Freecam) ? "[FREECAM]" :
                              (cam.mode == CameraMode::CCTV) ? "[CCTV]" :
                              (cam.mode == CameraMode::CinematicOrbit) ? "[ORBIT]" :
                              (cam.mode == CameraMode::LookAtTarget) ? "[LOOK-AT]" : "[STATIC]";

        std::string label = std::string(modeTag) + " " + cam.name + (isActive ? " (ACTIVE)" : "");
        if (ImGui::Selectable(label.c_str(), isSelected)) {
            SetSelectedIndex(i, true);
        }
        ImGui::PopStyleColor();

        // Action buttons right below or aligned
        ImGui::SameLine(ImGui::GetWindowWidth() - 220);
        if (!isActive) {
            if (ImGui::SmallButton("Look")) {
                SetActiveIndex(i, activeCamera, renderer);
            }
            ImGui::SameLine();
        }

        if (ImGui::SmallButton(cam.isWindowOpen ? "Window" : "Open")) {
            ToggleCameraWindow(i);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy")) {
            DuplicateCamera(i);
            ImGui::PopID();
            break;
        }

        if (m_cameras.size() > 1) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) {
                RemoveCamera(i);
                ImGui::PopID();
                break;
            }
        }

        ImGui::PopID();
        ImGui::Separator();
    }

    ImGui::EndChild();
    ImGui::End();
}

void CameraSystem::DrawWorldCameraGizmos(Renderer* renderer, Camera* activeCamera, float screenWidth, float screenHeight) {
    if (!activeCamera || m_cameras.empty() || screenWidth <= 0.0f || screenHeight <= 0.0f) return;

    XMMATRIX view = activeCamera->GetViewMatrix();
    float fov = XMConvertToRadians(renderer ? renderer->GetFOV() : 75.0f);
    float aspect = screenWidth / screenHeight;
    XMMATRIX proj = activeCamera->GetCleanProjectionMatrix(fov, aspect, 0.1f, 2000.0f);
    XMMATRIX viewProj = view * proj;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    for (size_t i = 0; i < m_cameras.size(); ++i) {
        if (i == m_activeCameraIndex) continue; // Don't draw gizmo for current active camera

        auto& cam = m_cameras[i];
        if (!cam.showGizmoInWorld) continue;

        // Project camera world position to screen space
        XMVECTOR worldPos = XMLoadFloat3(&cam.position);
        XMVECTOR clipPos = XMVector3TransformCoord(worldPos, viewProj);
        XMFLOAT3 clip;
        XMStoreFloat3(&clip, clipPos);

        // Check if inside frustum
        if (clip.z <= 0.0f || clip.z >= 1.0f || clip.x < -1.1f || clip.x > 1.1f || clip.y < -1.1f || clip.y > 1.1f) {
            continue;
        }

        float sx = (clip.x * 0.5f + 0.5f) * screenWidth;
        float sy = (-clip.y * 0.5f + 0.5f) * screenHeight;

        // Calculate look vector
        float pitchRad = XMConvertToRadians(cam.pitch);
        float yawRad = XMConvertToRadians(cam.yaw);
        XMVECTOR fwd = XMVectorSet(cosf(pitchRad) * sinf(yawRad), sinf(pitchRad), cosf(pitchRad) * cosf(yawRad), 0.0f);
        XMVECTOR tipWorld = XMVectorAdd(worldPos, XMVectorScale(fwd, 1.5f));
        XMVECTOR tipClip = XMVector3TransformCoord(tipWorld, viewProj);
        XMFLOAT3 tip;
        XMStoreFloat3(&tip, tipClip);

        if (tip.z > 0.0f && tip.z < 1.0f) {
            float tx = (tip.x * 0.5f + 0.5f) * screenWidth;
            float ty = (-tip.y * 0.5f + 0.5f) * screenHeight;
            drawList->AddLine(ImVec2(sx, sy), ImVec2(tx, ty), IM_COL32(255, 220, 40, 220), 2.0f);
        }

        // Camera Icon Circle
        drawList->AddCircleFilled(ImVec2(sx, sy), 7.0f, IM_COL32(30, 150, 255, 230));
        drawList->AddCircle(ImVec2(sx, sy), 9.0f, IM_COL32(255, 255, 255, 240), 16, 1.5f);

        // Badge label
        std::string badge = "Camera: " + cam.name;
        ImVec2 textSize = ImGui::CalcTextSize(badge.c_str());
        ImVec2 badgeMin(sx - textSize.x * 0.5f - 4.0f, sy + 12.0f);
        ImVec2 badgeMax(sx + textSize.x * 0.5f + 4.0f, sy + 12.0f + textSize.y + 4.0f);

        drawList->AddRectFilled(badgeMin, badgeMax, IM_COL32(15, 20, 30, 210), 3.0f);
        drawList->AddRect(badgeMin, badgeMax, IM_COL32(60, 120, 200, 220), 3.0f);
        drawList->AddText(ImVec2(badgeMin.x + 4.0f, badgeMin.y + 2.0f), IM_COL32(230, 240, 255, 255), badge.c_str());

        // Click detection on badge to open camera window
        ImVec2 mouse = ImGui::GetIO().MousePos;
        if (mouse.x >= badgeMin.x && mouse.x <= badgeMax.x && mouse.y >= badgeMin.y && mouse.y <= badgeMax.y) {
            drawList->AddRect(badgeMin, badgeMax, IM_COL32(255, 220, 50, 255), 3.0f, 0, 1.5f);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().WantCaptureMouse) {
                SetSelectedIndex(i, true);
            }
        }
    }
}

void CameraSystem::DrawUI(Renderer* renderer, Camera* activeCamera, float screenWidth, float screenHeight) {
    // 1. Draw Manager / Hierarchy Window
    DrawCameraManagerWindow(renderer, activeCamera);

    
    for (size_t i = 0; i < m_cameras.size(); ++i) {
        if (m_cameras[i].isWindowOpen) {
            DrawIndividualCameraWindow(i, renderer, activeCamera);
        }
    }

    // 3. Draw 3D Camera Gizmos & Frustums in the Viewport
    DrawWorldCameraGizmos(renderer, activeCamera, screenWidth, screenHeight);
}

#else

void CameraSystem::DrawUI(Renderer* renderer, Camera* activeCamera, float screenWidth, float screenHeight) {}

#endif
